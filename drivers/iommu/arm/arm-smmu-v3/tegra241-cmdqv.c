// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2021-2024 NVIDIA CORPORATION & AFFILIATES. */

#define dev_fmt(fmt) "tegra241_cmdqv: " fmt

#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/iommufd.h>
#include <linux/iopoll.h>
#include <uapi/linux/iommufd.h>

#include <acpi/acpixf.h>

#include "arm-smmu-v3.h"

/* CMDQV register page base and size defines */
#define TEGRA241_CMDQV_CONFIG_BASE	(0)
#define TEGRA241_CMDQV_CONFIG_SIZE	(SZ_64K)
#define TEGRA241_VCMDQ_PAGE0_BASE	(TEGRA241_CMDQV_CONFIG_BASE + SZ_64K)
#define TEGRA241_VCMDQ_PAGE1_BASE	(TEGRA241_VCMDQ_PAGE0_BASE + SZ_64K)
#define TEGRA241_VINTF_PAGE_BASE	(TEGRA241_VCMDQ_PAGE1_BASE + SZ_64K)

/* CMDQV global base regs */
#define TEGRA241_CMDQV_CONFIG		0x0000
#define  CMDQV_EN			BIT(0)

#define TEGRA241_CMDQV_PARAM		0x0004
#define  CMDQV_NUM_SID_PER_VM_LOG2	GENMASK(15, 12)
#define  CMDQV_NUM_VINTF_LOG2		GENMASK(11, 8)
#define  CMDQV_NUM_VCMDQ_LOG2		GENMASK(7, 4)
#define  CMDQV_VER			GENMASK(3, 0)

#define TEGRA241_CMDQV_STATUS		0x0008
#define  CMDQV_ENABLED			BIT(0)

#define TEGRA241_CMDQV_VINTF_ERR_MAP	0x0014
#define TEGRA241_CMDQV_VINTF_INT_MASK	0x001C
#define TEGRA241_CMDQV_CMDQ_ERR_MAP(m)  (0x0024 + 0x4*(m))

#define TEGRA241_CMDQV_CMDQ_ALLOC(q)	(0x0200 + 0x4*(q))
#define  CMDQV_CMDQ_ALLOC_VINTF		GENMASK(20, 15)
#define  CMDQV_CMDQ_ALLOC_LVCMDQ	GENMASK(7, 1)
#define  CMDQV_CMDQ_ALLOCATED		BIT(0)

/* VINTF base regs */
#define TEGRA241_VINTF(v)		(0x1000 + 0x100*(v))

#define TEGRA241_VINTF_CONFIG		0x0000
#define  VINTF_HYP_OWN			BIT(17)
#define  VINTF_VMID			GENMASK(16, 1)
#define  VINTF_EN			BIT(0)

#define TEGRA241_VINTF_STATUS		0x0004
#define  VINTF_STATUS			GENMASK(3, 1)
#define  VINTF_ENABLED			BIT(0)

#define TEGRA241_VINTF_SID_MATCH(s)	(0x0040 + 0x4*(s))
#define TEGRA241_VINTF_SID_REPLACE(s)	(0x0080 + 0x4*(s))

#define TEGRA241_VINTF_LVCMDQ_ERR_MAP_64(m) \
					(0x00C0 + 0x8*(m))
#define  LVCMDQ_ERR_MAP_NUM_64		2

/* VCMDQ base regs */
/* -- PAGE0 -- */
#define TEGRA241_VCMDQ_PAGE0(q)		(TEGRA241_VCMDQ_PAGE0_BASE + 0x80*(q))

#define TEGRA241_VCMDQ_CONS		0x00000
#define  VCMDQ_CONS_ERR			GENMASK(30, 24)

#define TEGRA241_VCMDQ_PROD		0x00004

#define TEGRA241_VCMDQ_CONFIG		0x00008
#define  VCMDQ_EN			BIT(0)

#define TEGRA241_VCMDQ_STATUS		0x0000C
#define  VCMDQ_ENABLED			BIT(0)

#define TEGRA241_VCMDQ_GERROR		0x00010
#define TEGRA241_VCMDQ_GERRORN		0x00014

/* -- PAGE1 -- */
#define TEGRA241_VCMDQ_PAGE1(q)		(TEGRA241_VCMDQ_PAGE1_BASE + 0x80*(q))
#define  VCMDQ_ADDR			GENMASK(47, 5)
#define  VCMDQ_LOG2SIZE			GENMASK(4, 0)

#define TEGRA241_VCMDQ_BASE		0x00000
#define TEGRA241_VCMDQ_CONS_INDX_BASE	0x00008

/* VINTF logical-VCMDQ pages */
#define TEGRA241_VINTFi_PAGE0(i)	(TEGRA241_VINTF_PAGE_BASE + SZ_128K*(i))
#define TEGRA241_VINTFi_PAGE1(i)	(TEGRA241_VINTFi_PAGE0(i) + SZ_64K)
#define TEGRA241_VINTFi_LVCMDQ_PAGE0(i, q) \
					(TEGRA241_VINTFi_PAGE0(i) + 0x80*(q))
#define TEGRA241_VINTFi_LVCMDQ_PAGE1(i, q) \
					(TEGRA241_VINTFi_PAGE1(i) + 0x80*(q))

/* MMIO helpers */
#define REG_CMDQV(_cmdqv, _regname) \
	((_cmdqv)->base + TEGRA241_CMDQV_##_regname)
#define REG_VINTF(_vintf, _regname) \
	((_vintf)->base + TEGRA241_VINTF_##_regname)
#define REG_VCMDQ_PAGE0(_vcmdq, _regname) \
	((_vcmdq)->page0 + TEGRA241_VCMDQ_##_regname)
#define REG_VCMDQ_PAGE1(_vcmdq, _regname) \
	((_vcmdq)->page1 + TEGRA241_VCMDQ_##_regname)


static bool disable_cmdqv;
module_param(disable_cmdqv, bool, 0444);
MODULE_PARM_DESC(disable_cmdqv,
	"This allows to disable CMDQV HW and use default SMMU internal CMDQ.");

static bool bypass_vcmdq;
module_param(bypass_vcmdq, bool, 0444);
MODULE_PARM_DESC(bypass_vcmdq,
	"This allows to bypass VCMDQ for debugging use or perf comparison.");

/**
 * struct tegra241_vcmdq - Virtual Command Queue
 * @core: Embedded iommufd_hw_queue structure
 * @idx: Global index in the CMDQV
 * @lidx: Local index in the VINTF
 * @enabled: Enable status
 * @cmdqv: Parent CMDQV pointer
 * @vintf: Parent VINTF pointer
 * @prev: Previous LVCMDQ to depend on
 * @cmdq: Command Queue struct
 * @page0: MMIO Page0 base address
 * @page1: MMIO Page1 base address
 */
struct tegra241_vcmdq {
	struct iommufd_hw_queue core;

	u16 idx;
	u16 lidx;

	bool enabled;

	struct tegra241_cmdqv *cmdqv;
	struct tegra241_vintf *vintf;
	struct tegra241_vcmdq *prev;
	struct arm_smmu_cmdq cmdq;

	void __iomem *page0;
	void __iomem *page1;
};
#define hw_queue_to_vcmdq(v) container_of(v, struct tegra241_vcmdq, core)

/**
 * struct tegra241_vintf - Virtual Interface
 * @vsmmu: Embedded arm_vsmmu structure
 * @idx: Global index in the CMDQV
 * @enabled: Enable status
 * @hyp_own: Owned by hypervisor (in-kernel)
 * @cmdqv: Parent CMDQV pointer
 * @lvcmdqs: List of logical VCMDQ pointers
 * @lvcmdq_mutex: Lock to serialize user-allocated lvcmdqs
 * @base: MMIO base address
 * @mmap_offset: Offset argument for mmap() syscall
 * @sids: Stream ID mapping resources
 */
struct tegra241_vintf {
	struct arm_vsmmu vsmmu;

	u16 idx;

	bool enabled;
	bool hyp_own;

	struct tegra241_cmdqv *cmdqv;
	struct tegra241_vcmdq **lvcmdqs;
	struct mutex lvcmdq_mutex; /* user space race */

	void __iomem *base;
	unsigned long mmap_offset;

	struct ida sids;
};
#define viommu_to_vintf(v) container_of(v, struct tegra241_vintf, vsmmu.core)

/**
 * struct tegra241_vintf_sid - Virtual Interface Stream ID Mapping
 * @core: Embedded iommufd_vdevice structure, holding virtual Stream ID
 * @vintf: Parent VINTF pointer
 * @sid: Physical Stream ID
 * @idx: Mapping index in the VINTF
 */
struct tegra241_vintf_sid {
	struct iommufd_vdevice core;
	struct tegra241_vintf *vintf;
	u32 sid;
	u8 idx;
};
#define vdev_to_vsid(v) container_of(v, struct tegra241_vintf_sid, core)

/**
 * struct tegra241_cmdqv - CMDQ-V for SMMUv3
 * @smmu: SMMUv3 device
 * @dev: CMDQV device
 * @base: MMIO base address
 * @base_phys: MMIO physical base address, for mmap
 * @irq: IRQ number
 * @num_vintfs: Total number of VINTFs
 * @num_vcmdqs: Total number of VCMDQs
 * @num_lvcmdqs_per_vintf: Number of logical VCMDQs per VINTF
 * @num_sids_per_vintf: Total number of SID mappings per VINTF
 * @vintf_ids: VINTF id allocator
 * @vintfs: List of VINTFs
 */
struct tegra241_cmdqv {
	struct arm_smmu_device smmu;
	struct device *dev;

	void __iomem *base;
	phys_addr_t base_phys;
	int irq;

	/* CMDQV Hardware Params */
	u16 num_vintfs;
	u16 num_vcmdqs;
	u16 num_lvcmdqs_per_vintf;
	u16 num_sids_per_vintf;

	struct ida vintf_ids;

	struct tegra241_vintf **vintfs;
};

/* Config and Polling Helpers */

static inline int tegra241_cmdqv_write_config(struct tegra241_cmdqv *cmdqv,
					      void __iomem *addr_config,
					      void __iomem *addr_status,
					      u32 regval, const char *header,
					      bool *out_enabled)
{
	bool en = regval & BIT(0);
	int ret;

	writel(regval, addr_config);
	ret = readl_poll_timeout(addr_status, regval,
				 en ? regval & BIT(0) : !(regval & BIT(0)),
				 1, ARM_SMMU_POLL_TIMEOUT_US);
	if (ret)
		dev_err(cmdqv->dev, "%sfailed to %sable, STATUS=0x%08X\n",
			header, en ? "en" : "dis", regval);
	if (out_enabled)
		WRITE_ONCE(*out_enabled, regval & BIT(0));
	return ret;
}

static inline int cmdqv_write_config(struct tegra241_cmdqv *cmdqv, u32 regval)
{
	return tegra241_cmdqv_write_config(cmdqv,
					   REG_CMDQV(cmdqv, CONFIG),
					   REG_CMDQV(cmdqv, STATUS),
					   regval, "CMDQV: ", NULL);
}

static inline int vintf_write_config(struct tegra241_vintf *vintf, u32 regval)
{
	char header[16];

	snprintf(header, 16, "VINTF%u: ", vintf->idx);
	return tegra241_cmdqv_write_config(vintf->cmdqv,
					   REG_VINTF(vintf, CONFIG),
					   REG_VINTF(vintf, STATUS),
					   regval, header, &vintf->enabled);
}

static inline char *lvcmdq_error_header(struct tegra241_vcmdq *vcmdq,
					char *header, int hlen)
{
	WARN_ON(hlen < 64);
	if (WARN_ON(!vcmdq->vintf))
		return "";
	snprintf(header, hlen, "VINTF%u: VCMDQ%u/LVCMDQ%u: ",
		 vcmdq->vintf->idx, vcmdq->idx, vcmdq->lidx);
	return header;
}

static inline int vcmdq_write_config(struct tegra241_vcmdq *vcmdq, u32 regval)
{
	char header[64], *h = lvcmdq_error_header(vcmdq, header, 64);

	return tegra241_cmdqv_write_config(vcmdq->cmdqv,
					   REG_VCMDQ_PAGE0(vcmdq, CONFIG),
					   REG_VCMDQ_PAGE0(vcmdq, STATUS),
					   regval, h, &vcmdq->enabled);
}

/* ISR Functions */

static void tegra241_vintf_user_handle_error(struct tegra241_vintf *vintf)
{
	struct iommufd_viommu *viommu = &vintf->vsmmu.core;
	struct iommu_vevent_tegra241_cmdqv vevent_data;
	int i;

	for (i = 0; i < LVCMDQ_ERR_MAP_NUM_64; i++) {
		u64 err = readq_relaxed(REG_VINTF(vintf, LVCMDQ_ERR_MAP_64(i)));

		vevent_data.lvcmdq_err_map[i] = cpu_to_le64(err);
	}

	iommufd_viommu_report_event(viommu, IOMMU_VEVENTQ_TYPE_TEGRA241_CMDQV,
				    &vevent_data, sizeof(vevent_data));
}

static void tegra241_vintf0_handle_error(struct tegra241_vintf *vintf)
{
	int i;

	for (i = 0; i < LVCMDQ_ERR_MAP_NUM_64; i++) {
		u64 map = readq_relaxed(REG_VINTF(vintf, LVCMDQ_ERR_MAP_64(i)));

		while (map) {
			unsigned long lidx = __ffs64(map);
			struct tegra241_vcmdq *vcmdq = vintf->lvcmdqs[lidx];
			u32 gerror = readl_relaxed(REG_VCMDQ_PAGE0(vcmdq, GERROR));

			__arm_smmu_cmdq_skip_err(&vintf->cmdqv->smmu, &vcmdq->cmdq);
			writel(gerror, REG_VCMDQ_PAGE0(vcmdq, GERRORN));
			map &= ~BIT_ULL(lidx);
		}
	}
}

static irqreturn_t tegra241_cmdqv_isr(int irq, void *devid)
{
	struct tegra241_cmdqv *cmdqv = (struct tegra241_cmdqv *)devid;
	void __iomem *reg_vintf_map = REG_CMDQV(cmdqv, VINTF_ERR_MAP);
	char err_str[256];
	u64 vintf_map;

	/* Use readl_relaxed() as register addresses are not 64-bit aligned */
	vintf_map = (u64)readl_relaxed(reg_vintf_map + 0x4) << 32 |
		    (u64)readl_relaxed(reg_vintf_map);

	snprintf(err_str, sizeof(err_str),
		 "vintf_map: %016llx, vcmdq_map %08x:%08x:%08x:%08x", vintf_map,
		 readl_relaxed(REG_CMDQV(cmdqv, CMDQ_ERR_MAP(3))),
		 readl_relaxed(REG_CMDQV(cmdqv, CMDQ_ERR_MAP(2))),
		 readl_relaxed(REG_CMDQV(cmdqv, CMDQ_ERR_MAP(1))),
		 readl_relaxed(REG_CMDQV(cmdqv, CMDQ_ERR_MAP(0))));

	dev_warn(cmdqv->dev, "unexpected error reported. %s\n", err_str);

	/* Handle VINTF0 and its LVCMDQs */
	if (vintf_map & BIT_ULL(0)) {
		tegra241_vintf0_handle_error(cmdqv->vintfs[0]);
		vintf_map &= ~BIT_ULL(0);
	}

	/* Handle other user VINTFs and their LVCMDQs */
	while (vintf_map) {
		unsigned long idx = __ffs64(vintf_map);

		tegra241_vintf_user_handle_error(cmdqv->vintfs[idx]);
		vintf_map &= ~BIT_ULL(idx);
	}

	return IRQ_HANDLED;
}

/* Command Queue Function */

static bool tegra241_guest_vcmdq_supports_cmd(struct arm_smmu_cmdq_ent *ent)
{
	switch (ent->opcode) {
	case CMDQ_OP_TLBI_NH_ASID:
	case CMDQ_OP_TLBI_NH_VA:
	case CMDQ_OP_ATC_INV:
		return true;
	default:
		return false;
	}
}

static struct arm_smmu_cmdq *
tegra241_cmdqv_get_cmdq(struct arm_smmu_device *smmu,
			struct arm_smmu_cmdq_ent *ent)
{
	struct tegra241_cmdqv *cmdqv =
		container_of(smmu, struct tegra241_cmdqv, smmu);
	struct tegra241_vintf *vintf = cmdqv->vintfs[0];
	struct tegra241_vcmdq *vcmdq;
	u16 lidx;

	if (READ_ONCE(bypass_vcmdq))
		return NULL;

	/* Use SMMU CMDQ if VINTF0 is uninitialized */
	if (!READ_ONCE(vintf->enabled))
		return NULL;

	/*
	 * Select a LVCMDQ to use. Here we use a temporal solution to
	 * balance out traffic on cmdq issuing: each cmdq has its own
	 * lock, if all cpus issue cmdlist using the same cmdq, only
	 * one CPU at a time can enter the process, while the others
	 * will be spinning at the same lock.
	 */
	lidx = raw_smp_processor_id() % cmdqv->num_lvcmdqs_per_vintf;
	vcmdq = vintf->lvcmdqs[lidx];
	if (!vcmdq || !READ_ONCE(vcmdq->enabled))
		return NULL;

	/* Unsupported CMD goes for smmu->cmdq pathway */
	if (!arm_smmu_cmdq_supports_cmd(&vcmdq->cmdq, ent))
		return NULL;
	return &vcmdq->cmdq;
}

/* HW Reset Functions */

/*
 * When a guest-owned VCMDQ is disabled, if the guest did not enqueue a CMD_SYNC
 * following an ATC_INV command at the end of the guest queue while this ATC_INV
 * is timed out, the TIMEOUT will not be reported until this VCMDQ gets assigned
 * to the next VM, which will be a false alarm potentially causing some unwanted
 * behavior in the new VM. Thus, a guest-owned VCMDQ must flush the TIMEOUT when
 * it gets disabled. This can be done by just issuing a CMD_SYNC to SMMU CMDQ.
 */
static void tegra241_vcmdq_hw_flush_timeout(struct tegra241_vcmdq *vcmdq)
{
	struct arm_smmu_device *smmu = &vcmdq->cmdqv->smmu;
	u64 cmd_sync[CMDQ_ENT_DWORDS] = {};

	cmd_sync[0] = FIELD_PREP(CMDQ_0_OP, CMDQ_OP_CMD_SYNC) |
		      FIELD_PREP(CMDQ_SYNC_0_CS, CMDQ_SYNC_0_CS_NONE);

	/*
	 * It does not hurt to insert another CMD_SYNC, taking advantage of the
	 * arm_smmu_cmdq_issue_cmdlist() that waits for the CMD_SYNC completion.
	 */
	arm_smmu_cmdq_issue_cmdlist(smmu, &smmu->cmdq, cmd_sync, 1, true);
}

/* This function is for LVCMDQ, so @vcmdq must not be unmapped yet */
static void tegra241_vcmdq_hw_deinit(struct tegra241_vcmdq *vcmdq)
{
	char header[64], *h = lvcmdq_error_header(vcmdq, header, 64);
	u32 gerrorn, gerror;

	if (vcmdq_write_config(vcmdq, 0)) {
		dev_err(vcmdq->cmdqv->dev,
			"%sGERRORN=0x%X, GERROR=0x%X, CONS=0x%X\n", h,
			readl_relaxed(REG_VCMDQ_PAGE0(vcmdq, GERRORN)),
			readl_relaxed(REG_VCMDQ_PAGE0(vcmdq, GERROR)),
			readl_relaxed(REG_VCMDQ_PAGE0(vcmdq, CONS)));
	}
	tegra241_vcmdq_hw_flush_timeout(vcmdq);

	writel_relaxed(0, REG_VCMDQ_PAGE0(vcmdq, PROD));
	writel_relaxed(0, REG_VCMDQ_PAGE0(vcmdq, CONS));
	writeq_relaxed(0, REG_VCMDQ_PAGE1(vcmdq, BASE));
	writeq_relaxed(0, REG_VCMDQ_PAGE1(vcmdq, CONS_INDX_BASE));

	gerrorn = readl_relaxed(REG_VCMDQ_PAGE0(vcmdq, GERRORN));
	gerror = readl_relaxed(REG_VCMDQ_PAGE0(vcmdq, GERROR));
	if (gerror != gerrorn) {
		dev_warn(vcmdq->cmdqv->dev,
			 "%suncleared error detected, resetting\n", h);
		writel(gerror, REG_VCMDQ_PAGE0(vcmdq, GERRORN));
	}

	dev_dbg(vcmdq->cmdqv->dev, "%sdeinited\n", h);
}

/* This function is for LVCMDQ, so @vcmdq must be mapped prior */
static int tegra241_vcmdq_hw_init(struct tegra241_vcmdq *vcmdq)
{
	char header[64], *h = lvcmdq_error_header(vcmdq, header, 64);
	int ret;

	/* Reset VCMDQ */
	tegra241_vcmdq_hw_deinit(vcmdq);

	/* Configure and enable VCMDQ */
	writeq_relaxed(vcmdq->cmdq.q.q_base, REG_VCMDQ_PAGE1(vcmdq, BASE));

	ret = vcmdq_write_config(vcmdq, VCMDQ_EN);
	if (ret) {
		dev_err(vcmdq->cmdqv->dev,
			"%sGERRORN=0x%X, GERROR=0x%X, CONS=0x%X\n", h,
			readl_relaxed(REG_VCMDQ_PAGE0(vcmdq, GERRORN)),
			readl_relaxed(REG_VCMDQ_PAGE0(vcmdq, GERROR)),
			readl_relaxed(REG_VCMDQ_PAGE0(vcmdq, CONS)));
		return ret;
	}

	dev_dbg(vcmdq->cmdqv->dev, "%sinited\n", h);
	return 0;
}

/* Unmap a global VCMDQ from the pre-assigned LVCMDQ */
static void tegra241_vcmdq_unmap_lvcmdq(struct tegra241_vcmdq *vcmdq)
{
	u32 regval = readl(REG_CMDQV(vcmdq->cmdqv, CMDQ_ALLOC(vcmdq->idx)));
	char header[64], *h = lvcmdq_error_header(vcmdq, header, 64);

	writel(regval & ~CMDQV_CMDQ_ALLOCATED,
	       REG_CMDQV(vcmdq->cmdqv, CMDQ_ALLOC(vcmdq->idx)));
	dev_dbg(vcmdq->cmdqv->dev, "%sunmapped\n", h);
}

static void tegra241_vintf_hw_deinit(struct tegra241_vintf *vintf)
{
	u16 lidx = vintf->cmdqv->num_lvcmdqs_per_vintf;
	int sidx;

	/* HW requires to unmap LVCMDQs in descending order */
	while (lidx--) {
		if (vintf->lvcmdqs && vintf->lvcmdqs[lidx]) {
			tegra241_vcmdq_hw_deinit(vintf->lvcmdqs[lidx]);
			tegra241_vcmdq_unmap_lvcmdq(vintf->lvcmdqs[lidx]);
		}
	}
	vintf_write_config(vintf, 0);
	for (sidx = 0; sidx < vintf->cmdqv->num_sids_per_vintf; sidx++) {
		writel(0, REG_VINTF(vintf, SID_MATCH(sidx)));
		writel(0, REG_VINTF(vintf, SID_REPLACE(sidx)));
	}
}

/* Map a global VCMDQ to the pre-assigned LVCMDQ */
static void tegra241_vcmdq_map_lvcmdq(struct tegra241_vcmdq *vcmdq)
{
	u32 regval = readl(REG_CMDQV(vcmdq->cmdqv, CMDQ_ALLOC(vcmdq->idx)));
	char header[64], *h = lvcmdq_error_header(vcmdq, header, 64);

	writel(regval | CMDQV_CMDQ_ALLOCATED,
	       REG_CMDQV(vcmdq->cmdqv, CMDQ_ALLOC(vcmdq->idx)));
	dev_dbg(vcmdq->cmdqv->dev, "%smapped\n", h);
}

static int tegra241_vintf_hw_init(struct tegra241_vintf *vintf, bool hyp_own)
{
	u32 regval;
	u16 lidx;
	int ret;

	/* Reset VINTF */
	tegra241_vintf_hw_deinit(vintf);

	/* Configure and enable VINTF */
	/*
	 * Note that HYP_OWN bit is wired to zero when running in guest kernel,
	 * whether enabling it here or not, as !HYP_OWN cmdq HWs only support a
	 * restricted set of supported commands.
	 */
	regval = FIELD_PREP(VINTF_HYP_OWN, hyp_own) |
		 FIELD_PREP(VINTF_VMID, vintf->vsmmu.vmid);
	writel(regval, REG_VINTF(vintf, CONFIG));

	ret = vintf_write_config(vintf, regval | VINTF_EN);
	if (ret)
		return ret;
	/*
	 * As being mentioned above, HYP_OWN bit is wired to zero for a guest
	 * kernel, so read it back from HW to ensure that reflects in hyp_own
	 */
	vintf->hyp_own = !!(VINTF_HYP_OWN & readl(REG_VINTF(vintf, CONFIG)));

	/* HW requires to map LVCMDQs in ascending order */
	for (lidx = 0; lidx < vintf->cmdqv->num_lvcmdqs_per_vintf; lidx++) {
		if (vintf->lvcmdqs && vintf->lvcmdqs[lidx]) {
			tegra241_vcmdq_map_lvcmdq(vintf->lvcmdqs[lidx]);
			ret = tegra241_vcmdq_hw_init(vintf->lvcmdqs[lidx]);
			if (ret) {
				tegra241_vintf_hw_deinit(vintf);
				return ret;
			}
		}
	}

	return 0;
}

static int tegra241_cmdqv_hw_reset(struct arm_smmu_device *smmu)
{
	struct tegra241_cmdqv *cmdqv =
		container_of(smmu, struct tegra241_cmdqv, smmu);
	u16 qidx, lidx, idx;
	u32 regval;
	int ret;

	/* Reset CMDQV */
	regval = readl_relaxed(REG_CMDQV(cmdqv, CONFIG));
	ret = cmdqv_write_config(cmdqv, regval & ~CMDQV_EN);
	if (ret)
		return ret;
	ret = cmdqv_write_config(cmdqv, regval | CMDQV_EN);
	if (ret)
		return ret;

	/* Assign preallocated global VCMDQs to each VINTF as LVCMDQs */
	for (idx = 0, qidx = 0; idx < cmdqv->num_vintfs; idx++) {
		for (lidx = 0; lidx < cmdqv->num_lvcmdqs_per_vintf; lidx++) {
			regval  = FIELD_PREP(CMDQV_CMDQ_ALLOC_VINTF, idx);
			regval |= FIELD_PREP(CMDQV_CMDQ_ALLOC_LVCMDQ, lidx);
			writel_relaxed(regval,
				       REG_CMDQV(cmdqv, CMDQ_ALLOC(qidx++)));
		}
	}

	return tegra241_vintf_hw_init(cmdqv->vintfs[0], true);
}

/* VCMDQ Resource Helpers */

static int tegra241_vcmdq_alloc_smmu_cmdq(struct tegra241_vcmdq *vcmdq)
{
	struct arm_smmu_device *smmu = &vcmdq->cmdqv->smmu;
	struct arm_smmu_cmdq *cmdq = &vcmdq->cmdq;
	struct arm_smmu_queue *q = &cmdq->q;
	char name[16];
	u32 regval;
	int ret;

	snprintf(name, 16, "vcmdq%u", vcmdq->idx);

	/* Cap queue size to SMMU's IDR1.CMDQS and ensure natural alignment */
	regval = readl_relaxed(smmu->base + ARM_SMMU_IDR1);
	q->llq.max_n_shift =
		min_t(u32, CMDQ_MAX_SZ_SHIFT, FIELD_GET(IDR1_CMDQS, regval));

	/* Use the common helper to init the VCMDQ, and then... */
	ret = arm_smmu_init_one_queue(smmu, q, vcmdq->page0,
				      TEGRA241_VCMDQ_PROD, TEGRA241_VCMDQ_CONS,
				      CMDQ_ENT_DWORDS, name);
	if (ret)
		return ret;

	/* ...override q_base to write VCMDQ_BASE registers */
	q->q_base = q->base_dma & VCMDQ_ADDR;
	q->q_base |= FIELD_PREP(VCMDQ_LOG2SIZE, q->llq.max_n_shift);

	if (!vcmdq->vintf->hyp_own)
		cmdq->supports_cmd = tegra241_guest_vcmdq_supports_cmd;

	return arm_smmu_cmdq_init(smmu, cmdq);
}

/* VINTF Logical VCMDQ Resource Helpers */

static void tegra241_vintf_deinit_lvcmdq(struct tegra241_vintf *vintf, u16 lidx)
{
	vintf->lvcmdqs[lidx] = NULL;
}

static int tegra241_vintf_init_lvcmdq(struct tegra241_vintf *vintf, u16 lidx,
				      struct tegra241_vcmdq *vcmdq)
{
	struct tegra241_cmdqv *cmdqv = vintf->cmdqv;
	u16 idx = vintf->idx;

	vcmdq->idx = idx * cmdqv->num_lvcmdqs_per_vintf + lidx;
	vcmdq->lidx = lidx;
	vcmdq->cmdqv = cmdqv;
	vcmdq->vintf = vintf;
	vcmdq->page0 = cmdqv->base + TEGRA241_VINTFi_LVCMDQ_PAGE0(idx, lidx);
	vcmdq->page1 = cmdqv->base + TEGRA241_VINTFi_LVCMDQ_PAGE1(idx, lidx);

	vintf->lvcmdqs[lidx] = vcmdq;
	return 0;
}

static void tegra241_vintf_free_lvcmdq(struct tegra241_vintf *vintf, u16 lidx)
{
	struct tegra241_vcmdq *vcmdq = vintf->lvcmdqs[lidx];
	char header[64];

	/* Note that the lvcmdq queue memory space is managed by devres */

	tegra241_vintf_deinit_lvcmdq(vintf, lidx);

	dev_dbg(vintf->cmdqv->dev,
		"%sdeallocated\n", lvcmdq_error_header(vcmdq, header, 64));
	/* Guest-owned VCMDQ is free-ed with hw_queue by iommufd core */
	if (vcmdq->vintf->hyp_own)
		kfree(vcmdq);
}

static struct tegra241_vcmdq *
tegra241_vintf_alloc_lvcmdq(struct tegra241_vintf *vintf, u16 lidx)
{
	struct tegra241_cmdqv *cmdqv = vintf->cmdqv;
	struct tegra241_vcmdq *vcmdq;
	char header[64];
	int ret;

	vcmdq = kzalloc(sizeof(*vcmdq), GFP_KERNEL);
	if (!vcmdq)
		return ERR_PTR(-ENOMEM);

	ret = tegra241_vintf_init_lvcmdq(vintf, lidx, vcmdq);
	if (ret)
		goto free_vcmdq;

	/* Build an arm_smmu_cmdq for each LVCMDQ */
	ret = tegra241_vcmdq_alloc_smmu_cmdq(vcmdq);
	if (ret)
		goto deinit_lvcmdq;

	dev_dbg(cmdqv->dev,
		"%sallocated\n", lvcmdq_error_header(vcmdq, header, 64));
	return vcmdq;

deinit_lvcmdq:
	tegra241_vintf_deinit_lvcmdq(vintf, lidx);
free_vcmdq:
	kfree(vcmdq);
	return ERR_PTR(ret);
}

/* VINTF Resource Helpers */

static void tegra241_cmdqv_deinit_vintf(struct tegra241_cmdqv *cmdqv, u16 idx)
{
	kfree(cmdqv->vintfs[idx]->lvcmdqs);
	ida_free(&cmdqv->vintf_ids, idx);
	cmdqv->vintfs[idx] = NULL;
}

static int tegra241_cmdqv_init_vintf(struct tegra241_cmdqv *cmdqv, u16 max_idx,
				     struct tegra241_vintf *vintf)
{

	u16 idx;
	int ret;

	ret = ida_alloc_max(&cmdqv->vintf_ids, max_idx, GFP_KERNEL);
	if (ret < 0)
		return ret;
	idx = ret;

	vintf->idx = idx;
	vintf->cmdqv = cmdqv;
	vintf->base = cmdqv->base + TEGRA241_VINTF(idx);

	vintf->lvcmdqs = kcalloc(cmdqv->num_lvcmdqs_per_vintf,
				 sizeof(*vintf->lvcmdqs), GFP_KERNEL);
	if (!vintf->lvcmdqs) {
		ida_free(&cmdqv->vintf_ids, idx);
		return -ENOMEM;
	}

	cmdqv->vintfs[idx] = vintf;
	return ret;
}

/* Remove Helpers */

static void tegra241_cmdqv_remove_vintf(struct tegra241_cmdqv *cmdqv, u16 idx)
{
	struct tegra241_vintf *vintf = cmdqv->vintfs[idx];
	u16 lidx;

	tegra241_vintf_hw_deinit(vintf);

	/* Remove LVCMDQ resources */
	for (lidx = 0; lidx < vintf->cmdqv->num_lvcmdqs_per_vintf; lidx++)
		if (vintf->lvcmdqs[lidx])
			tegra241_vintf_free_lvcmdq(vintf, lidx);

	dev_dbg(cmdqv->dev, "VINTF%u: deallocated\n", vintf->idx);
	tegra241_cmdqv_deinit_vintf(cmdqv, idx);
	if (!vintf->hyp_own) {
		mutex_destroy(&vintf->lvcmdq_mutex);
		ida_destroy(&vintf->sids);
		/* Guest-owned VINTF is free-ed with viommu by iommufd core */
	} else {
		kfree(vintf);
	}
}

static void tegra241_cmdqv_remove(struct arm_smmu_device *smmu)
{
	struct tegra241_cmdqv *cmdqv =
		container_of(smmu, struct tegra241_cmdqv, smmu);
	u16 idx;

	/* Remove VINTF resources */
	for (idx = 0; idx < cmdqv->num_vintfs; idx++) {
		if (cmdqv->vintfs[idx]) {
			/* Only vintf0 should remain at this stage */
			WARN_ON(idx > 0);
			tegra241_cmdqv_remove_vintf(cmdqv, idx);
		}
	}

	/* Remove cmdqv resources */
	ida_destroy(&cmdqv->vintf_ids);

	if (cmdqv->irq > 0)
		free_irq(cmdqv->irq, cmdqv);
	iounmap(cmdqv->base);
	kfree(cmdqv->vintfs);
	put_device(cmdqv->dev); /* smmu->impl_dev */
}

static int
tegra241_cmdqv_init_vintf_user(struct arm_vsmmu *vsmmu,
			       const struct iommu_user_data *user_data);

static void *tegra241_cmdqv_hw_info(struct arm_smmu_device *smmu, u32 *length,
				    enum iommu_hw_info_type *type)
{
	struct tegra241_cmdqv *cmdqv =
		container_of(smmu, struct tegra241_cmdqv, smmu);
	struct iommu_hw_info_tegra241_cmdqv *info;
	u32 regval;

	if (*type != IOMMU_HW_INFO_TYPE_TEGRA241_CMDQV)
		return ERR_PTR(-EOPNOTSUPP);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	regval = readl_relaxed(REG_CMDQV(cmdqv, PARAM));
	info->log2vcmdqs = ilog2(cmdqv->num_lvcmdqs_per_vintf);
	info->log2vsids = ilog2(cmdqv->num_sids_per_vintf);
	info->version = FIELD_GET(CMDQV_VER, regval);

	*length = sizeof(*info);
	*type = IOMMU_HW_INFO_TYPE_TEGRA241_CMDQV;
	return info;
}

static size_t tegra241_cmdqv_get_vintf_size(enum iommu_viommu_type viommu_type)
{
	if (viommu_type != IOMMU_VIOMMU_TYPE_TEGRA241_CMDQV)
		return 0;
	return VIOMMU_STRUCT_SIZE(struct tegra241_vintf, vsmmu.core);
}

static struct arm_smmu_impl_ops tegra241_cmdqv_impl_ops = {
	/* For in-kernel use */
	.get_secondary_cmdq = tegra241_cmdqv_get_cmdq,
	.device_reset = tegra241_cmdqv_hw_reset,
	.device_remove = tegra241_cmdqv_remove,
	/* For user-space use */
	.hw_info = tegra241_cmdqv_hw_info,
	.get_viommu_size = tegra241_cmdqv_get_vintf_size,
	.vsmmu_init = tegra241_cmdqv_init_vintf_user,
};

/* Probe Functions */

static int tegra241_cmdqv_acpi_is_memory(struct acpi_resource *res, void *data)
{
	struct resource_win win;

	return !acpi_dev_resource_address_space(res, &win);
}

static int tegra241_cmdqv_acpi_get_irqs(struct acpi_resource *ares, void *data)
{
	struct resource r;
	int *irq = data;

	if (*irq <= 0 && acpi_dev_resource_interrupt(ares, 0, &r))
		*irq = r.start;
	return 1; /* No need to add resource to the list */
}

static struct resource *
tegra241_cmdqv_find_acpi_resource(struct device *dev, int *irq)
{
	struct acpi_device *adev = to_acpi_device(dev);
	struct list_head resource_list;
	struct resource_entry *rentry;
	struct resource *res = NULL;
	int ret;

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list,
				     tegra241_cmdqv_acpi_is_memory, NULL);
	if (ret < 0) {
		dev_err(dev, "failed to get memory resource: %d\n", ret);
		return NULL;
	}

	rentry = list_first_entry_or_null(&resource_list,
					  struct resource_entry, node);
	if (!rentry) {
		dev_err(dev, "failed to get memory resource entry\n");
		goto free_list;
	}

	/* Caller must free the res */
	res = kzalloc(sizeof(*res), GFP_KERNEL);
	if (!res)
		goto free_list;

	*res = *rentry->res;

	acpi_dev_free_resource_list(&resource_list);

	INIT_LIST_HEAD(&resource_list);

	if (irq)
		ret = acpi_dev_get_resources(adev, &resource_list,
					     tegra241_cmdqv_acpi_get_irqs, irq);
	if (ret < 0 || !irq || *irq <= 0)
		dev_warn(dev, "no interrupt. errors will not be reported\n");

free_list:
	acpi_dev_free_resource_list(&resource_list);
	return res;
}

static int tegra241_cmdqv_init_structures(struct arm_smmu_device *smmu)
{
	struct tegra241_cmdqv *cmdqv =
		container_of(smmu, struct tegra241_cmdqv, smmu);
	struct tegra241_vintf *vintf;
	int lidx;
	int ret;

	vintf = kzalloc(sizeof(*vintf), GFP_KERNEL);
	if (!vintf)
		return -ENOMEM;

	/* Init VINTF0 for in-kernel use */
	ret = tegra241_cmdqv_init_vintf(cmdqv, 0, vintf);
	if (ret) {
		dev_err(cmdqv->dev, "failed to init vintf0: %d\n", ret);
		return ret;
	}

	/* Preallocate logical VCMDQs to VINTF0 */
	for (lidx = 0; lidx < cmdqv->num_lvcmdqs_per_vintf; lidx++) {
		struct tegra241_vcmdq *vcmdq;

		vcmdq = tegra241_vintf_alloc_lvcmdq(vintf, lidx);
		if (IS_ERR(vcmdq))
			return PTR_ERR(vcmdq);
	}

	/* Now, we are ready to run all the impl ops */
	smmu->impl_ops = &tegra241_cmdqv_impl_ops;
	return 0;
}

#ifdef CONFIG_IOMMU_DEBUGFS
static struct dentry *cmdqv_debugfs_dir;
#endif

static struct arm_smmu_device *
__tegra241_cmdqv_probe(struct arm_smmu_device *smmu, struct resource *res,
		       int irq)
{
	static const struct arm_smmu_impl_ops init_ops = {
		.init_structures = tegra241_cmdqv_init_structures,
		.device_remove = tegra241_cmdqv_remove,
	};
	struct tegra241_cmdqv *cmdqv = NULL;
	struct arm_smmu_device *new_smmu;
	void __iomem *base;
	u32 regval;
	int ret;

	static_assert(offsetof(struct tegra241_cmdqv, smmu) == 0);

	base = ioremap(res->start, resource_size(res));
	if (!base) {
		dev_err(smmu->dev, "failed to ioremap\n");
		return NULL;
	}

	regval = readl(base + TEGRA241_CMDQV_CONFIG);
	if (disable_cmdqv) {
		dev_info(smmu->dev, "Detected disable_cmdqv=true\n");
		writel(regval & ~CMDQV_EN, base + TEGRA241_CMDQV_CONFIG);
		goto iounmap;
	}

	cmdqv = devm_krealloc(smmu->dev, smmu, sizeof(*cmdqv), GFP_KERNEL);
	if (!cmdqv)
		goto iounmap;
	new_smmu = &cmdqv->smmu;

	cmdqv->irq = irq;
	cmdqv->base = base;
	cmdqv->dev = smmu->impl_dev;
	cmdqv->base_phys = res->start;

	if (cmdqv->irq > 0) {
		ret = request_threaded_irq(irq, NULL, tegra241_cmdqv_isr,
					   IRQF_ONESHOT, "tegra241-cmdqv",
					   cmdqv);
		if (ret) {
			dev_err(cmdqv->dev, "failed to request irq (%d): %d\n",
				cmdqv->irq, ret);
			goto iounmap;
		}
	}

	regval = readl_relaxed(REG_CMDQV(cmdqv, PARAM));
	cmdqv->num_vintfs = 1 << FIELD_GET(CMDQV_NUM_VINTF_LOG2, regval);
	cmdqv->num_vcmdqs = 1 << FIELD_GET(CMDQV_NUM_VCMDQ_LOG2, regval);
	cmdqv->num_lvcmdqs_per_vintf = cmdqv->num_vcmdqs / cmdqv->num_vintfs;
	cmdqv->num_sids_per_vintf =
		1 << FIELD_GET(CMDQV_NUM_SID_PER_VM_LOG2, regval);

	cmdqv->vintfs =
		kcalloc(cmdqv->num_vintfs, sizeof(*cmdqv->vintfs), GFP_KERNEL);
	if (!cmdqv->vintfs)
		goto free_irq;

	ida_init(&cmdqv->vintf_ids);

#ifdef CONFIG_IOMMU_DEBUGFS
	if (!cmdqv_debugfs_dir) {
		cmdqv_debugfs_dir =
			debugfs_create_dir("tegra241_cmdqv", iommu_debugfs_dir);
		debugfs_create_bool("bypass_vcmdq", 0644, cmdqv_debugfs_dir,
				    &bypass_vcmdq);
	}
#endif

	/* Provide init-level ops only, until tegra241_cmdqv_init_structures */
	new_smmu->impl_ops = &init_ops;

	return new_smmu;

free_irq:
	if (cmdqv->irq > 0)
		free_irq(cmdqv->irq, cmdqv);
iounmap:
	iounmap(base);
	return NULL;
}

struct arm_smmu_device *tegra241_cmdqv_probe(struct arm_smmu_device *smmu)
{
	struct arm_smmu_device *new_smmu;
	struct resource *res = NULL;
	int irq;

	if (!smmu->dev->of_node)
		res = tegra241_cmdqv_find_acpi_resource(smmu->impl_dev, &irq);
	if (!res)
		goto out_fallback;

	new_smmu = __tegra241_cmdqv_probe(smmu, res, irq);
	kfree(res);

	if (new_smmu)
		return new_smmu;

out_fallback:
	dev_info(smmu->impl_dev, "Falling back to standard SMMU CMDQ\n");
	smmu->options &= ~ARM_SMMU_OPT_TEGRA241_CMDQV;
	put_device(smmu->impl_dev);
	return ERR_PTR(-ENODEV);
}

/* User space VINTF and VCMDQ Functions */

static size_t tegra241_vintf_get_vcmdq_size(struct iommufd_viommu *viommu,
					    enum iommu_hw_queue_type queue_type)
{
	if (queue_type != IOMMU_HW_QUEUE_TYPE_TEGRA241_CMDQV)
		return 0;
	return HW_QUEUE_STRUCT_SIZE(struct tegra241_vcmdq, core);
}

static int tegra241_vcmdq_hw_init_user(struct tegra241_vcmdq *vcmdq)
{
	char header[64];

	/* Configure the vcmdq only; User space does the enabling */
	writeq_relaxed(vcmdq->cmdq.q.q_base, REG_VCMDQ_PAGE1(vcmdq, BASE));

	dev_dbg(vcmdq->cmdqv->dev, "%sinited at host PA 0x%llx size 0x%lx\n",
		lvcmdq_error_header(vcmdq, header, 64),
		vcmdq->cmdq.q.q_base & VCMDQ_ADDR,
		1UL << (vcmdq->cmdq.q.q_base & VCMDQ_LOG2SIZE));
	return 0;
}

static void
tegra241_vintf_destroy_lvcmdq_user(struct iommufd_hw_queue *hw_queue)
{
	struct tegra241_vcmdq *vcmdq = hw_queue_to_vcmdq(hw_queue);

	mutex_lock(&vcmdq->vintf->lvcmdq_mutex);
	tegra241_vcmdq_hw_deinit(vcmdq);
	tegra241_vcmdq_unmap_lvcmdq(vcmdq);
	tegra241_vintf_free_lvcmdq(vcmdq->vintf, vcmdq->lidx);
	if (vcmdq->prev)
		iommufd_hw_queue_undepend(vcmdq, vcmdq->prev, core);
	mutex_unlock(&vcmdq->vintf->lvcmdq_mutex);
}

static int tegra241_vintf_alloc_lvcmdq_user(struct iommufd_hw_queue *hw_queue,
					    u32 lidx, phys_addr_t base_addr_pa)
{
	struct tegra241_vintf *vintf = viommu_to_vintf(hw_queue->viommu);
	struct tegra241_vcmdq *vcmdq = hw_queue_to_vcmdq(hw_queue);
	struct tegra241_cmdqv *cmdqv = vintf->cmdqv;
	struct arm_smmu_device *smmu = &cmdqv->smmu;
	struct tegra241_vcmdq *prev = NULL;
	u32 log2size, max_n_shift;
	char header[64];
	int ret;

	if (hw_queue->type != IOMMU_HW_QUEUE_TYPE_TEGRA241_CMDQV)
		return -EOPNOTSUPP;
	if (lidx >= cmdqv->num_lvcmdqs_per_vintf)
		return -EINVAL;

	mutex_lock(&vintf->lvcmdq_mutex);

	if (vintf->lvcmdqs[lidx]) {
		ret = -EEXIST;
		goto unlock;
	}

	/*
	 * HW requires to map LVCMDQs in ascending order, so reject if the
	 * previous lvcmdqs is not allocated yet.
	 */
	if (lidx) {
		prev = vintf->lvcmdqs[lidx - 1];
		if (!prev) {
			ret = -EIO;
			goto unlock;
		}
	}

	/*
	 * hw_queue->length must be a power of 2, in range of
	 *   [ 32, 2 ^ (idr[1].CMDQS + CMDQ_ENT_SZ_SHIFT) ]
	 */
	max_n_shift = FIELD_GET(IDR1_CMDQS,
				readl_relaxed(smmu->base + ARM_SMMU_IDR1));
	if (!is_power_of_2(hw_queue->length) || hw_queue->length < 32 ||
	    hw_queue->length > (1 << (max_n_shift + CMDQ_ENT_SZ_SHIFT))) {
		ret = -EINVAL;
		goto unlock;
	}
	log2size = ilog2(hw_queue->length) - CMDQ_ENT_SZ_SHIFT;

	/* base_addr_pa must be aligned to hw_queue->length */
	if (base_addr_pa & ~VCMDQ_ADDR ||
	    base_addr_pa & (hw_queue->length - 1)) {
		ret = -EINVAL;
		goto unlock;
	}

	/*
	 * HW requires to unmap LVCMDQs in descending order, so destroy() must
	 * follow this rule. Set a dependency on its previous LVCMDQ so iommufd
	 * core will help enforce it.
	 */
	if (prev) {
		ret = iommufd_hw_queue_depend(vcmdq, prev, core);
		if (ret)
			goto unlock;
	}
	vcmdq->prev = prev;

	ret = tegra241_vintf_init_lvcmdq(vintf, lidx, vcmdq);
	if (ret)
		goto undepend_vcmdq;

	dev_dbg(cmdqv->dev, "%sallocated\n",
		lvcmdq_error_header(vcmdq, header, 64));

	tegra241_vcmdq_map_lvcmdq(vcmdq);

	vcmdq->cmdq.q.q_base = base_addr_pa & VCMDQ_ADDR;
	vcmdq->cmdq.q.q_base |= log2size;

	ret = tegra241_vcmdq_hw_init_user(vcmdq);
	if (ret)
		goto unmap_lvcmdq;

	hw_queue->destroy = &tegra241_vintf_destroy_lvcmdq_user;
	mutex_unlock(&vintf->lvcmdq_mutex);
	return 0;

unmap_lvcmdq:
	tegra241_vcmdq_unmap_lvcmdq(vcmdq);
	tegra241_vintf_deinit_lvcmdq(vintf, lidx);
undepend_vcmdq:
	if (vcmdq->prev)
		iommufd_hw_queue_undepend(vcmdq, vcmdq->prev, core);
unlock:
	mutex_unlock(&vintf->lvcmdq_mutex);
	return ret;
}

static void tegra241_cmdqv_destroy_vintf_user(struct iommufd_viommu *viommu)
{
	struct tegra241_vintf *vintf = viommu_to_vintf(viommu);

	if (vintf->mmap_offset)
		iommufd_viommu_destroy_mmap(&vintf->vsmmu.core,
					    vintf->mmap_offset);
	tegra241_cmdqv_remove_vintf(vintf->cmdqv, vintf->idx);
}

static void tegra241_vintf_destroy_vsid(struct iommufd_vdevice *vdev)
{
	struct tegra241_vintf_sid *vsid = vdev_to_vsid(vdev);
	struct tegra241_vintf *vintf = vsid->vintf;

	writel(0, REG_VINTF(vintf, SID_MATCH(vsid->idx)));
	writel(0, REG_VINTF(vintf, SID_REPLACE(vsid->idx)));
	ida_free(&vintf->sids, vsid->idx);
	dev_dbg(vintf->cmdqv->dev,
		"VINTF%u: deallocated SID_REPLACE%d for pSID=%x\n", vintf->idx,
		vsid->idx, vsid->sid);
}

static int tegra241_vintf_init_vsid(struct iommufd_vdevice *vdev)
{
	struct device *dev = iommufd_vdevice_to_device(vdev);
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);
	struct tegra241_vintf *vintf = viommu_to_vintf(vdev->viommu);
	struct tegra241_vintf_sid *vsid = vdev_to_vsid(vdev);
	struct arm_smmu_stream *stream = &master->streams[0];
	u64 virt_sid = vdev->virt_id;
	int sidx;

	if (virt_sid > UINT_MAX)
		return -EINVAL;

	WARN_ON_ONCE(master->num_streams != 1);

	/* Find an empty pair of SID_REPLACE and SID_MATCH */
	sidx = ida_alloc_max(&vintf->sids, vintf->cmdqv->num_sids_per_vintf - 1,
			     GFP_KERNEL);
	if (sidx < 0)
		return sidx;

	writel(stream->id, REG_VINTF(vintf, SID_REPLACE(sidx)));
	writel(virt_sid << 1 | 0x1, REG_VINTF(vintf, SID_MATCH(sidx)));
	dev_dbg(vintf->cmdqv->dev,
		"VINTF%u: allocated SID_REPLACE%d for pSID=%x, vSID=%x\n",
		vintf->idx, sidx, stream->id, (u32)virt_sid);

	vsid->idx = sidx;
	vsid->vintf = vintf;
	vsid->sid = stream->id;

	vdev->destroy = &tegra241_vintf_destroy_vsid;
	return 0;
}

static struct iommufd_viommu_ops tegra241_cmdqv_viommu_ops = {
	.destroy = tegra241_cmdqv_destroy_vintf_user,
	.alloc_domain_nested = arm_vsmmu_alloc_domain_nested,
	/* Non-accelerated commands will be still handled by the kernel */
	.cache_invalidate = arm_vsmmu_cache_invalidate,
	.vdevice_size = VDEVICE_STRUCT_SIZE(struct tegra241_vintf_sid, core),
	.vdevice_init = tegra241_vintf_init_vsid,
	.get_hw_queue_size = tegra241_vintf_get_vcmdq_size,
	.hw_queue_init_phys = tegra241_vintf_alloc_lvcmdq_user,
};

static int
tegra241_cmdqv_init_vintf_user(struct arm_vsmmu *vsmmu,
			       const struct iommu_user_data *user_data)
{
	struct tegra241_cmdqv *cmdqv =
		container_of(vsmmu->smmu, struct tegra241_cmdqv, smmu);
	struct tegra241_vintf *vintf = viommu_to_vintf(&vsmmu->core);
	struct iommu_viommu_tegra241_cmdqv data;
	phys_addr_t page0_base;
	int ret;

	/*
	 * Unsupported type should be rejected by tegra241_cmdqv_get_vintf_size.
	 * Seeing one here indicates a kernel bug or some data corruption.
	 */
	if (WARN_ON(vsmmu->core.type != IOMMU_VIOMMU_TYPE_TEGRA241_CMDQV))
		return -EOPNOTSUPP;

	if (!user_data)
		return -EINVAL;

	ret = iommu_copy_struct_from_user(&data, user_data,
					  IOMMU_VIOMMU_TYPE_TEGRA241_CMDQV,
					  out_vintf_mmap_length);
	if (ret)
		return ret;

	ret = tegra241_cmdqv_init_vintf(cmdqv, cmdqv->num_vintfs - 1, vintf);
	if (ret < 0) {
		dev_err(cmdqv->dev, "no more available vintf\n");
		return ret;
	}

	/*
	 * Initialize the user-owned VINTF without a LVCMDQ, as it cannot pre-
	 * allocate a LVCMDQ until user space wants one, for security reasons.
	 * It is different than the kernel-owned VINTF0, which had pre-assigned
	 * and pre-allocated global VCMDQs that would be mapped to the LVCMDQs
	 * by the tegra241_vintf_hw_init() call.
	 */
	ret = tegra241_vintf_hw_init(vintf, false);
	if (ret)
		goto deinit_vintf;

	page0_base = cmdqv->base_phys + TEGRA241_VINTFi_PAGE0(vintf->idx);
	ret = iommufd_viommu_alloc_mmap(&vintf->vsmmu.core, page0_base, SZ_64K,
					&vintf->mmap_offset);
	if (ret)
		goto hw_deinit_vintf;

	data.out_vintf_mmap_length = SZ_64K;
	data.out_vintf_mmap_offset = vintf->mmap_offset;
	ret = iommu_copy_struct_to_user(user_data, &data,
					IOMMU_VIOMMU_TYPE_TEGRA241_CMDQV,
					out_vintf_mmap_length);
	if (ret)
		goto free_mmap;

	ida_init(&vintf->sids);
	mutex_init(&vintf->lvcmdq_mutex);

	dev_dbg(cmdqv->dev, "VINTF%u: allocated with vmid (%d)\n", vintf->idx,
		vintf->vsmmu.vmid);

	vsmmu->core.ops = &tegra241_cmdqv_viommu_ops;
	return 0;

free_mmap:
	iommufd_viommu_destroy_mmap(&vintf->vsmmu.core, vintf->mmap_offset);
hw_deinit_vintf:
	tegra241_vintf_hw_deinit(vintf);
deinit_vintf:
	tegra241_cmdqv_deinit_vintf(cmdqv, vintf->idx);
	return ret;
}

MODULE_IMPORT_NS("IOMMUFD");
