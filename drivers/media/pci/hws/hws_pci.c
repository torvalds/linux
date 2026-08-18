// SPDX-License-Identifier: GPL-2.0-only
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/iopoll.h>
#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/pm.h>
#include <linux/freezer.h>
#include <linux/pci_regs.h>

#include <media/v4l2-ctrls.h>

#include "hws.h"
#include "hws_reg.h"
#include "hws_video.h"
#include "hws_irq.h"
#include "hws_v4l2_ioctl.h"

#define DRV_NAME "hws"
#define HWS_BUSY_POLL_DELAY_US 10
#define HWS_BUSY_POLL_TIMEOUT_US 1000000

static unsigned long long hws_elapsed_us(u64 start_ns)
{
	return div_u64(ktime_get_mono_fast_ns() - start_ns, 1000);
}

/* register layout inside HWS_REG_DEVICE_INFO */
#define DEVINFO_VER GENMASK(7, 0)
#define DEVINFO_SUBVER GENMASK(15, 8)
#define DEVINFO_YV12 GENMASK(31, 28)
#define DEVINFO_HWKEY GENMASK(27, 24)
#define DEVINFO_PORTID GENMASK(25, 24) /* low 2 bits of HW-key */

#define MAKE_ENTRY(__vend, __chip, __subven, __subdev, __configptr) \
	{ .vendor = (__vend),                                       \
	  .device = (__chip),                                       \
	  .subvendor = (__subven),                                  \
	  .subdevice = (__subdev),                                  \
	  .driver_data = (unsigned long)(__configptr) }

/*
 * PCI IDs for HWS family cards.
 *
 * The subsystem IDs are fixed at 0x8888:0x0007 for this family. Some boards
 * enumerate with vendor ID 0x8888 or 0x1f33. Exact SKU names are not fully
 * pinned down yet; update these comments when vendor documentation or INF
 * strings are available.
 */
static const struct pci_device_id hws_pci_table[] = {
	/* HWS family, SKU unknown. */
	MAKE_ENTRY(0x8888, 0x9534, 0x8888, 0x0007, NULL),
	MAKE_ENTRY(0x1F33, 0x8534, 0x8888, 0x0007, NULL),
	MAKE_ENTRY(0x1F33, 0x8554, 0x8888, 0x0007, NULL),

	/* HWS 2x2 HDMI family. */
	MAKE_ENTRY(0x8888, 0x8524, 0x8888, 0x0007, NULL),
	/* HWS 2x2 SDI family. */
	MAKE_ENTRY(0x1F33, 0x6524, 0x8888, 0x0007, NULL),

	/* HWS X4 HDMI family. */
	MAKE_ENTRY(0x8888, 0x8504, 0x8888, 0x0007, NULL),
	/* HWS X4 SDI family. */
	MAKE_ENTRY(0x8888, 0x6504, 0x8888, 0x0007, NULL),

	/* HWS family, SKU unknown. */
	MAKE_ENTRY(0x8888, 0x8532, 0x8888, 0x0007, NULL),
	MAKE_ENTRY(0x8888, 0x8512, 0x8888, 0x0007, NULL),
	MAKE_ENTRY(0x8888, 0x8501, 0x8888, 0x0007, NULL),
	MAKE_ENTRY(0x1F33, 0x6502, 0x8888, 0x0007, NULL),

	/* HWS X4 HDMI family (alternate vendor ID). */
	MAKE_ENTRY(0x1F33, 0x8504, 0x8888, 0x0007, NULL),
	/* HWS 2x2 HDMI family (alternate vendor ID). */
	MAKE_ENTRY(0x1F33, 0x8524, 0x8888, 0x0007, NULL),

	{}
};

static void enable_pcie_relaxed_ordering(struct pci_dev *dev)
{
	pcie_capability_set_word(dev, PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_RELAX_EN);
}

static void hws_configure_hardware_capabilities(struct hws_pcie_dev *hdev)
{
	u16 id = hdev->device_id;

	/* select per-chip channel counts */
	switch (id) {
	case 0x9534:
	case 0x6524:
	case 0x8524:
	case 0x8504:
	case 0x6504:
		hdev->cur_max_video_ch = 4;
		break;
	case 0x8532:
		hdev->cur_max_video_ch = 2;
		break;
	case 0x8512:
	case 0x6502:
		hdev->cur_max_video_ch = 2;
		break;
	case 0x8501:
		hdev->cur_max_video_ch = 1;
		break;
	default:
		hdev->cur_max_video_ch = 4;
		break;
	}

	/* universal buffer capacity */
	hdev->max_hw_video_buf_sz = MAX_MM_VIDEO_SIZE;

	/* decide hardware-version and program DMA max size if needed */
	if (hdev->device_ver > 121) {
		if (id == 0x8501 && hdev->device_ver == 122) {
			hdev->hw_ver = 0;
		} else {
			hdev->hw_ver = 1;
			u32 dma_max = (u32)(MAX_VIDEO_SCALER_SIZE / 16);

			writel(dma_max, hdev->bar0_base + HWS_REG_DMA_MAX_SIZE);
			/* readback to flush posted MMIO write */
			(void)readl(hdev->bar0_base + HWS_REG_DMA_MAX_SIZE);
		}
	} else {
		hdev->hw_ver = 0;
	}
}

static void hws_stop_device(struct hws_pcie_dev *hws);

static void hws_log_lifecycle_snapshot(struct hws_pcie_dev *hws,
				       const char *action,
				       const char *phase)
{
	struct device *dev;
	u32 int_en, int_status, vcap, sys_status, dec_mode;

	if (!hws || !hws->pdev)
		return;

	dev = &hws->pdev->dev;
	if (!hws->bar0_base) {
		dev_dbg(dev,
			"lifecycle:%s:%s bar0-unmapped suspended=%d start_run=%d pci_lost=%d irq=%d\n",
			action, phase, READ_ONCE(hws->suspended), hws->start_run,
			hws->pci_lost, hws->irq);
		return;
	}

	int_en = readl(hws->bar0_base + INT_EN_REG_BASE);
	int_status = readl(hws->bar0_base + HWS_REG_INT_STATUS);
	vcap = readl(hws->bar0_base + HWS_REG_VCAP_ENABLE);
	sys_status = readl(hws->bar0_base + HWS_REG_SYS_STATUS);
	dec_mode = readl(hws->bar0_base + HWS_REG_DEC_MODE);

	dev_dbg(dev,
		"lifecycle:%s:%s suspended=%d start_run=%d pci_lost=%d irq=%d INT_EN=0x%08x INT_STATUS=0x%08x VCAP=0x%08x SYS=0x%08x DEC=0x%08x\n",
		action, phase, READ_ONCE(hws->suspended), hws->start_run,
		hws->pci_lost, hws->irq, int_en, int_status, vcap,
		sys_status, dec_mode);
}

static int read_chip_id(struct hws_pcie_dev *hdev)
{
	u32 reg;
	/* mirror PCI IDs for later switches */
	hdev->device_id = hdev->pdev->device;
	hdev->vendor_id = hdev->pdev->vendor;

	reg = readl(hdev->bar0_base + HWS_REG_DEVICE_INFO);

	hdev->device_ver = FIELD_GET(DEVINFO_VER, reg);
	hdev->sub_ver = FIELD_GET(DEVINFO_SUBVER, reg);
	hdev->support_yv12 = FIELD_GET(DEVINFO_YV12, reg);
	hdev->port_id = FIELD_GET(DEVINFO_PORTID, reg);

	hdev->max_hw_video_buf_sz = MAX_MM_VIDEO_SIZE;
	hdev->max_channels = 4;
	hdev->buf_allocated = false;
	hdev->main_task = NULL;
	hdev->start_run = false;
	hdev->pci_lost = 0;

	writel(0x00, hdev->bar0_base + HWS_REG_DEC_MODE);
	writel(0x10, hdev->bar0_base + HWS_REG_DEC_MODE);

	hws_configure_hardware_capabilities(hdev);

	dev_info(&hdev->pdev->dev,
		 "chip detected: ver=%u subver=%u port=%u yv12=%u\n",
		 hdev->device_ver, hdev->sub_ver, hdev->port_id,
		 hdev->support_yv12);

	return 0;
}

static int main_ks_thread_handle(void *data)
{
	struct hws_pcie_dev *pdx = data;

	set_freezable();

	while (!kthread_should_stop()) {
		/* If we're suspending, don't touch hardware; just sleep/freeze. */
		if (READ_ONCE(pdx->suspended)) {
			try_to_freeze();
			schedule_timeout_interruptible(msecs_to_jiffies(1000));
			continue;
		}

		/* avoid MMIO when suspended (guarded above) */
		check_video_format(pdx);

		try_to_freeze(); /* cooperate with freezer each loop */

		/* Sleep 1s or until signaled to wake/stop */
		schedule_timeout_interruptible(msecs_to_jiffies(1000));
	}

	dev_dbg(&pdx->pdev->dev, "%s: exiting\n", __func__);
	return 0;
}

static void hws_stop_kthread_action(void *data)
{
	struct hws_pcie_dev *hws = data;
	struct task_struct *t;
	u64 start_ns;

	if (!hws)
		return;

	t = READ_ONCE(hws->main_task);
	if (!IS_ERR_OR_NULL(t)) {
		start_ns = ktime_get_mono_fast_ns();
		dev_dbg(&hws->pdev->dev,
			"lifecycle:kthread-stop:begin task=%s[%d]\n",
			t->comm, t->pid);
		WRITE_ONCE(hws->main_task, NULL);
		kthread_stop(t);
		dev_dbg(&hws->pdev->dev,
			"lifecycle:kthread-stop:done (%lluus)\n",
			hws_elapsed_us(start_ns));
	}
}

static int hws_alloc_seed_buffers(struct hws_pcie_dev *hws)
{
	int ch;
	/* 64 KiB is plenty for a safe dummy; hardware needs 64-byte alignment. */
	const size_t need = ALIGN(64 * 1024, 64);

	for (ch = 0; ch < hws->cur_max_video_ch; ch++) {
#if defined(CONFIG_HAS_DMA) /* normal on PCIe platforms */
		void *cpu = dma_alloc_coherent(&hws->pdev->dev, need,
					       &hws->scratch_vid[ch].dma,
					       GFP_KERNEL);
#else
		void *cpu = NULL;
#endif
		if (!cpu) {
			dev_warn(&hws->pdev->dev,
				 "scratch: dma_alloc_coherent failed ch=%d\n", ch);
			/* not fatal: free earlier ones and continue without seeding */
			while (--ch >= 0) {
				if (hws->scratch_vid[ch].cpu)
					dma_free_coherent(&hws->pdev->dev,
							  hws->scratch_vid[ch].size,
							  hws->scratch_vid[ch].cpu,
							  hws->scratch_vid[ch].dma);
				hws->scratch_vid[ch].cpu = NULL;
				hws->scratch_vid[ch].size = 0;
			}
			return -ENOMEM;
		}
		hws->scratch_vid[ch].cpu  = cpu;
		hws->scratch_vid[ch].size = need;
	}
	return 0;
}

static void hws_free_seed_buffers(struct hws_pcie_dev *hws)
{
	int ch;

	for (ch = 0; ch < hws->cur_max_video_ch; ch++) {
		if (hws->scratch_vid[ch].cpu) {
			dma_free_coherent(&hws->pdev->dev,
					  hws->scratch_vid[ch].size,
					  hws->scratch_vid[ch].cpu,
					  hws->scratch_vid[ch].dma);
			hws->scratch_vid[ch].cpu = NULL;
			hws->scratch_vid[ch].size = 0;
		}
	}
}

static void hws_seed_channel(struct hws_pcie_dev *hws, int ch)
{
	dma_addr_t paddr = hws->scratch_vid[ch].dma;
	u32 lo = lower_32_bits(paddr);
	u32 hi = upper_32_bits(paddr);
	u32 pci_addr = lo & PCI_E_BAR_ADD_LOWMASK;

	lo &= PCI_E_BAR_ADD_MASK;

	/* Program 64-bit BAR remap entry for this channel (table @ 0x208 + ch * 8) */
	writel_relaxed(hi, hws->bar0_base +
			    PCI_ADDR_TABLE_BASE + 0x208 + ch * 8);
	writel_relaxed(lo, hws->bar0_base +
			    PCI_ADDR_TABLE_BASE + 0x208 + ch * 8 +
			    PCIE_BARADDROFSIZE);

	/* Program capture engine per-channel base/half */
	writel_relaxed((ch + 1) * PCIEBAR_AXI_BASE + pci_addr,
		       hws->bar0_base + CVBS_IN_BUF_BASE +
		       ch * PCIE_BARADDROFSIZE);

	/* Half size: use either the current format's half or half of scratch. */
	{
		u32 half = hws->video[ch].pix.half_size ?
			hws->video[ch].pix.half_size :
			(u32)(hws->scratch_vid[ch].size / 2);

		writel_relaxed(half / 16,
			       hws->bar0_base + CVBS_IN_BUF_BASE2 +
			       ch * PCIE_BARADDROFSIZE);
	}

	(void)readl(hws->bar0_base + HWS_REG_INT_STATUS); /* flush posted writes */
}

static void hws_seed_all_channels(struct hws_pcie_dev *hws)
{
	int ch;

	for (ch = 0; ch < hws->cur_max_video_ch; ch++) {
		if (hws->scratch_vid[ch].cpu)
			hws_seed_channel(hws, ch);
	}
}

static void hws_irq_mask_gate(struct hws_pcie_dev *hws)
{
	writel(0x00000000, hws->bar0_base + INT_EN_REG_BASE);
	(void)readl(hws->bar0_base + INT_EN_REG_BASE);
}

static void hws_irq_unmask_gate(struct hws_pcie_dev *hws)
{
	writel(HWS_INT_EN_MASK, hws->bar0_base + INT_EN_REG_BASE);
	(void)readl(hws->bar0_base + INT_EN_REG_BASE);
}

static void hws_irq_clear_pending(struct hws_pcie_dev *hws)
{
	u32 st = readl(hws->bar0_base + HWS_REG_INT_STATUS);

	if (st) {
		writel(st, hws->bar0_base + HWS_REG_INT_STATUS); /* W1C */
		(void)readl(hws->bar0_base + HWS_REG_INT_STATUS);
	}
}

static void hws_block_hotpaths(struct hws_pcie_dev *hws)
{
	WRITE_ONCE(hws->suspended, true);
	if (hws->irq >= 0)
		disable_irq(hws->irq);

	if (!hws->bar0_base)
		return;

	hws_irq_mask_gate(hws);
	hws_irq_clear_pending(hws);
}

static int hws_probe(struct pci_dev *pdev, const struct pci_device_id *pci_id)
{
	struct hws_pcie_dev *hws;
	int i, ret, irq;
	unsigned long irqf = 0;
	bool v4l2_registered = false;

	/* devres-backed device object */
	hws = devm_kzalloc(&pdev->dev, sizeof(*hws), GFP_KERNEL);
	if (!hws)
		return -ENOMEM;

	hws->pdev = pdev;
	hws->irq = -1;
	hws->suspended = false;
	pci_set_drvdata(pdev, hws);

	/* 1) Enable device + bus mastering (managed) */
	ret = pcim_enable_device(pdev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "pcim_enable_device\n");
	pci_set_master(pdev);

	/* 2) Map BAR0 (managed) */
	ret = pcim_iomap_regions(pdev, BIT(0), KBUILD_MODNAME);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "pcim_iomap_regions BAR0\n");
	hws->bar0_base = pcim_iomap_table(pdev)[0];

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_warn(&pdev->dev,
			 "64-bit DMA mask unavailable, falling back to 32-bit (%d)\n",
			 ret);
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "No suitable DMA configuration\n");
	} else {
		dev_dbg(&pdev->dev, "Using 64-bit DMA mask\n");
	}

	/* 3) Apply optional PCIe tuning. */
	enable_pcie_relaxed_ordering(pdev);
#ifdef CONFIG_ARCH_TI816X
	pcie_set_readrq(pdev, 128);
#endif

	/* 4) Identify chip & capabilities */
	read_chip_id(hws);
	dev_info(&pdev->dev, "Device VID=0x%04x DID=0x%04x\n",
		 pdev->vendor, pdev->device);
	hws_init_video_sys(hws, false);

	/* 5) Init channels (video state, locks, vb2, ctrls) */
	for (i = 0; i < hws->max_channels; i++) {
		ret = hws_video_init_channel(hws, i);
		if (ret) {
			dev_err(&pdev->dev, "video channel init failed (ch=%d)\n", i);
			goto err_unwind_channels;
		}
	}

	/* 6) Allocate scratch DMA and seed BAR table + channel base/half (legacy SetDMAAddress) */
	ret = hws_alloc_seed_buffers(hws);
	if (!ret)
		hws_seed_all_channels(hws);

	/* 7) Start-run sequence. */
	hws_init_video_sys(hws, false);

	/* A) Force legacy INTx; legacy used request_irq(pdev->irq, ..., IRQF_SHARED) */
	pci_intx(pdev, 1);
	irqf = IRQF_SHARED;
	irq = pdev->irq;
	hws->irq = irq;
	dev_info(&pdev->dev, "IRQ mode: legacy INTx (shared), irq=%d\n", irq);

	/* B) Mask the device's global/bridge gate (INT_EN_REG_BASE) */
	hws_irq_mask_gate(hws);

	/* C) Clear any sticky pending interrupt status (W1C) before we arm the line */
	hws_irq_clear_pending(hws);

	/* D) Request the legacy shared interrupt line (no vectors/MSI/MSI-X) */
	ret = devm_request_irq(&pdev->dev, irq, hws_irq_handler, irqf,
			       dev_name(&pdev->dev), hws);
	if (ret) {
		dev_err(&pdev->dev, "request_irq(%d) failed: %d\n", irq, ret);
		goto err_unwind_channels;
	}

	/* E) Set the global interrupt enable bit in main control register */
	{
		u32 ctl_reg = readl(hws->bar0_base + HWS_REG_CTL);

		ctl_reg |= HWS_CTL_IRQ_ENABLE_BIT;
		writel(ctl_reg, hws->bar0_base + HWS_REG_CTL);
		(void)readl(hws->bar0_base + HWS_REG_CTL); /* flush write */
		dev_info(&pdev->dev, "Global IRQ enable bit set in control register\n");
	}

	/* F) Open the global gate just like legacy did */
	hws_irq_unmask_gate(hws);
	dev_info(&pdev->dev, "INT_EN_GATE readback=0x%08x\n",
		 readl(hws->bar0_base + INT_EN_REG_BASE));

	/* 11) Register V4L2 */
	ret = hws_video_register(hws);
	if (ret) {
		dev_err(&pdev->dev, "video_register: %d\n", ret);
		goto err_unwind_channels;
	}
	v4l2_registered = true;

	/* 12) Background monitor thread (managed) */
	hws->main_task = kthread_run(main_ks_thread_handle, hws, "hws-mon");
	if (IS_ERR(hws->main_task)) {
		ret = PTR_ERR(hws->main_task);
		hws->main_task = NULL;
		dev_err(&pdev->dev, "kthread_run: %d\n", ret);
		goto err_unregister_va;
	}
	ret = devm_add_action_or_reset(&pdev->dev, hws_stop_kthread_action, hws);
	if (ret) {
		dev_err(&pdev->dev, "devm_add_action kthread_stop: %d\n", ret);
		goto err_unregister_va; /* reset already stopped the thread */
	}

	/* 13) Final: show the line is armed */
	dev_info(&pdev->dev, "irq handler installed on irq=%d\n", irq);
	return 0;

err_unregister_va:
	hws_stop_device(hws);
	hws_video_unregister(hws);
	hws_free_seed_buffers(hws);
	return ret;
err_unwind_channels:
	hws_free_seed_buffers(hws);
	if (!v4l2_registered) {
		while (--i >= 0)
			hws_video_cleanup_channel(hws, i);
	}
	return ret;
}

static int hws_check_busy(struct hws_pcie_dev *pdx)
{
	void __iomem *reg = pdx->bar0_base + HWS_REG_SYS_STATUS;
	u32 val;
	int ret;

	/* poll until !(val & BUSY_BIT), sleeping HWS_BUSY_POLL_DELAY_US between reads */
	ret = readl_poll_timeout(reg, val, !(val & HWS_SYS_DMA_BUSY_BIT),
				 HWS_BUSY_POLL_DELAY_US,
				 HWS_BUSY_POLL_TIMEOUT_US);
	if (ret) {
		dev_err(&pdx->pdev->dev,
			"SYS_STATUS busy bit never cleared (0x%08x)\n", val);
		return -ETIMEDOUT;
	}

	return 0;
}

static void hws_stop_dsp(struct hws_pcie_dev *hws)
{
	u32 status;

	/* Read the decoder mode/status register */
	status = readl(hws->bar0_base + HWS_REG_DEC_MODE);
	dev_dbg(&hws->pdev->dev, "%s: status=0x%08x\n", __func__, status);

	/* If the device looks unplugged/stuck, bail out */
	if (status == 0xFFFFFFFF)
		return;

	/* Tell the DSP to stop */
	writel(0x10, hws->bar0_base + HWS_REG_DEC_MODE);

	if (hws_check_busy(hws))
		dev_warn(&hws->pdev->dev, "DSP busy timeout on stop\n");
	/* Disable video capture engine in the DSP */
	writel(0x0, hws->bar0_base + HWS_REG_VCAP_ENABLE);
}

/* Publish stop so ISR/BH will not touch video buffers anymore. */
static void hws_publish_stop_flags(struct hws_pcie_dev *hws)
{
	unsigned int i;

	for (i = 0; i < hws->cur_max_video_ch; ++i) {
		struct hws_video *v = &hws->video[i];

		WRITE_ONCE(v->cap_active,     false);
		WRITE_ONCE(v->stop_requested, true);
	}

	smp_wmb(); /* make flags visible before we touch MMIO/queues */
}

/* Drain engines + ISR/BH after flags are published. */
static void hws_drain_after_stop(struct hws_pcie_dev *hws)
{
	u32 ackmask = 0;
	unsigned int i;
	u64 start_ns = ktime_get_mono_fast_ns();

	/* Mask device enables: no new DMA starts. */
	writel(0x0, hws->bar0_base + HWS_REG_VCAP_ENABLE);
	(void)readl(hws->bar0_base + HWS_REG_INT_STATUS); /* flush */

	/* Let any in-flight DMAs finish (best-effort). */
	(void)hws_check_busy(hws);

	/* Ack any latched VDONE. */
	for (i = 0; i < hws->cur_max_video_ch; ++i)
		ackmask |= HWS_INT_VDONE_BIT(i);
	if (ackmask) {
		writel(ackmask, hws->bar0_base + HWS_REG_INT_STATUS);
		(void)readl(hws->bar0_base + HWS_REG_INT_STATUS);
	}

	/* Ensure no hard IRQ is still running. */
	if (hws->irq >= 0)
		synchronize_irq(hws->irq);

	dev_dbg(&hws->pdev->dev, "lifecycle:drain-after-stop:done (%lluus)\n",
		hws_elapsed_us(start_ns));
}

static void hws_stop_device(struct hws_pcie_dev *hws)
{
	u32 status = readl(hws->bar0_base + HWS_REG_SYS_STATUS);
	u64 start_ns = ktime_get_mono_fast_ns();
	bool live = status != 0xFFFFFFFF;

	dev_dbg(&hws->pdev->dev, "%s: status=0x%08x\n", __func__, status);
	if (!live) {
		hws->pci_lost = true;
		goto out;
	}
	hws_log_lifecycle_snapshot(hws, "stop-device", "begin");

	/* Make ISR/BH a no-op, then drain engines/IRQ. */
	hws_publish_stop_flags(hws);
	hws_drain_after_stop(hws);

	/* 1) Stop the on-board DSP */
	hws_stop_dsp(hws);

out:
	hws->start_run = false;
	if (live)
		hws_log_lifecycle_snapshot(hws, "stop-device", "end");
	else
		dev_dbg(&hws->pdev->dev, "lifecycle:stop-device:device-lost\n");
	dev_dbg(&hws->pdev->dev, "lifecycle:stop-device:done (%lluus)\n",
		hws_elapsed_us(start_ns));
	dev_dbg(&hws->pdev->dev, "%s: complete\n", __func__);
}

static int hws_quiesce_for_transition(struct hws_pcie_dev *hws,
				      const char *action,
				      bool stop_thread)
{
	struct device *dev = &hws->pdev->dev;
	u64 start_ns = ktime_get_mono_fast_ns();
	u64 step_ns;
	int vret;

	hws_log_lifecycle_snapshot(hws, action, "begin");

	step_ns = ktime_get_mono_fast_ns();
	hws_block_hotpaths(hws);
	dev_dbg(dev, "lifecycle:%s:block-hotpaths (%lluus)\n", action,
		hws_elapsed_us(step_ns));
	hws_log_lifecycle_snapshot(hws, action, "blocked");

	if (stop_thread) {
		step_ns = ktime_get_mono_fast_ns();
		hws_stop_kthread_action(hws);
		dev_dbg(dev, "lifecycle:%s:stop-kthread (%lluus)\n", action,
			hws_elapsed_us(step_ns));
	}

	step_ns = ktime_get_mono_fast_ns();
	vret = hws_video_quiesce(hws, action);
	dev_dbg(dev, "lifecycle:%s:video-quiesce ret=%d (%lluus)\n", action,
		vret, hws_elapsed_us(step_ns));
	if (vret)
		dev_warn(dev, "lifecycle:%s video quiesce returned %d\n",
			 action, vret);

	step_ns = ktime_get_mono_fast_ns();
	hws_stop_device(hws);
	dev_dbg(dev, "lifecycle:%s:stop-device (%lluus)\n", action,
		hws_elapsed_us(step_ns));
	hws_log_lifecycle_snapshot(hws, action, "end");
	dev_dbg(dev, "lifecycle:%s:quiesce-done ret=%d (%lluus)\n", action,
		vret, hws_elapsed_us(start_ns));

	return vret;
}

static void hws_remove(struct pci_dev *pdev)
{
	struct hws_pcie_dev *hws = pci_get_drvdata(pdev);
	u64 start_ns;

	if (!hws)
		return;

	start_ns = ktime_get_mono_fast_ns();
	dev_info(&pdev->dev, "lifecycle:remove begin\n");
	hws_log_lifecycle_snapshot(hws, "remove", "begin");

	/* Stop the monitor thread before tearing down V4L2/vb2 objects. */
	hws_block_hotpaths(hws);
	hws_stop_kthread_action(hws);

	/* Stop hardware and capture cleanly. */
	hws_stop_device(hws);

	/* Unregister V4L2 resources. */
	hws_video_unregister(hws);

	/* Release seeded DMA buffers */
	hws_free_seed_buffers(hws);
	/* kthread is stopped by the devm action registered in probe. */
	hws_log_lifecycle_snapshot(hws, "remove", "end");
	dev_info(&pdev->dev, "lifecycle:remove done (%lluus)\n",
		 hws_elapsed_us(start_ns));
}

#ifdef CONFIG_PM_SLEEP
static int hws_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct hws_pcie_dev *hws = pci_get_drvdata(pdev);
	int vret;
	u64 start_ns = ktime_get_mono_fast_ns();
	u64 step_ns;

	dev_info(dev, "lifecycle:pm_suspend begin\n");
	vret = hws_quiesce_for_transition(hws, "pm_suspend", false);

	step_ns = ktime_get_mono_fast_ns();
	pci_save_state(pdev);
	pci_clear_master(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);
	dev_dbg(dev, "lifecycle:pm_suspend:pci-d3hot (%lluus)\n",
		hws_elapsed_us(step_ns));
	dev_info(dev, "lifecycle:pm_suspend done ret=%d (%lluus)\n", vret,
		 hws_elapsed_us(start_ns));

	return 0;
}

static int hws_pm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct hws_pcie_dev *hws = pci_get_drvdata(pdev);
	int ret;
	u64 start_ns = ktime_get_mono_fast_ns();
	u64 step_ns;

	dev_info(dev, "lifecycle:pm_resume begin\n");

	/* Back to D0 and re-enable the function */
	step_ns = ktime_get_mono_fast_ns();
	pci_set_power_state(pdev, PCI_D0);

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(dev, "pci_enable_device: %d\n", ret);
		return ret;
	}
	pci_restore_state(pdev);
	pci_set_master(pdev);
	dev_dbg(dev, "lifecycle:pm_resume:pci-enable (%lluus)\n",
		hws_elapsed_us(step_ns));

	/* Reapply any PCIe tuning lost across D3 */
	enable_pcie_relaxed_ordering(pdev);

	/* Reinitialize chip-side capabilities / registers */
	step_ns = ktime_get_mono_fast_ns();
	read_chip_id(hws);
	/* Re-seed BAR remaps/DMA windows and restart the capture core */
	hws_seed_all_channels(hws);
	hws_init_video_sys(hws, true);
	hws_irq_clear_pending(hws);
	dev_dbg(dev, "lifecycle:pm_resume:chip-reinit (%lluus)\n",
		hws_elapsed_us(step_ns));

	/* IRQs can be re-enabled now that MMIO is sane */
	step_ns = ktime_get_mono_fast_ns();
	if (hws->irq >= 0)
		enable_irq(hws->irq);

	WRITE_ONCE(hws->suspended, false);
	dev_dbg(dev, "lifecycle:pm_resume:irq-unsuspend (%lluus)\n",
		hws_elapsed_us(step_ns));

	/* vb2: nothing mandatory; userspace will STREAMON again when ready */
	step_ns = ktime_get_mono_fast_ns();
	hws_video_pm_resume(hws);
	dev_dbg(dev, "lifecycle:pm_resume:video-resume (%lluus)\n",
		hws_elapsed_us(step_ns));
	hws_log_lifecycle_snapshot(hws, "pm_resume", "end");
	dev_info(dev, "lifecycle:pm_resume done (%lluus)\n",
		 hws_elapsed_us(start_ns));

	return 0;
}

static SIMPLE_DEV_PM_OPS(hws_pm_ops, hws_pm_suspend, hws_pm_resume);
# define HWS_PM_OPS (&hws_pm_ops)
#else
# define HWS_PM_OPS NULL
#endif

static void hws_shutdown(struct pci_dev *pdev)
{
	struct hws_pcie_dev *hws = pci_get_drvdata(pdev);
	int vret = 0;
	u64 start_ns = ktime_get_mono_fast_ns();
	u64 step_ns;

	if (!hws)
		return;

	dev_info(&pdev->dev, "lifecycle:pci_shutdown begin\n");
	vret = hws_quiesce_for_transition(hws, "pci_shutdown", true);

	step_ns = ktime_get_mono_fast_ns();
	pci_clear_master(pdev);
	dev_dbg(&pdev->dev, "lifecycle:pci_shutdown:clear-master (%lluus)\n",
		hws_elapsed_us(step_ns));
	dev_info(&pdev->dev, "lifecycle:pci_shutdown done ret=%d (%lluus)\n",
		 vret, hws_elapsed_us(start_ns));
}

static struct pci_driver hws_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = hws_pci_table,
	.probe = hws_probe,
	.remove = hws_remove,
	.shutdown = hws_shutdown,
	.driver = {
		.pm = HWS_PM_OPS,
	},
};

MODULE_DEVICE_TABLE(pci, hws_pci_table);

static int __init pcie_hws_init(void)
{
	return pci_register_driver(&hws_pci_driver);
}

static void __exit pcie_hws_exit(void)
{
	pci_unregister_driver(&hws_pci_driver);
}

module_init(pcie_hws_init);
module_exit(pcie_hws_exit);

MODULE_DESCRIPTION(DRV_NAME);
MODULE_AUTHOR("Ben Hoff <hoff.benjamin.k@gmail.com>");
MODULE_AUTHOR("Sales <sales@avmatrix.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("DMA_BUF");
