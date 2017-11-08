/*
 * Copyright 2016-17 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) "vas: " fmt

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <linux/rcupdate.h>
#include <linux/cred.h>

#include "vas.h"
#include "copy-paste.h"

/*
 * Compute the paste address region for the window @window using the
 * ->paste_base_addr and ->paste_win_id_shift we got from device tree.
 */
static void compute_paste_address(struct vas_window *window, u64 *addr, int *len)
{
	int winid;
	u64 base, shift;

	base = window->vinst->paste_base_addr;
	shift = window->vinst->paste_win_id_shift;
	winid = window->winid;

	*addr  = base + (winid << shift);
	if (len)
		*len = PAGE_SIZE;

	pr_debug("Txwin #%d: Paste addr 0x%llx\n", winid, *addr);
}

static inline void get_hvwc_mmio_bar(struct vas_window *window,
			u64 *start, int *len)
{
	u64 pbaddr;

	pbaddr = window->vinst->hvwc_bar_start;
	*start = pbaddr + window->winid * VAS_HVWC_SIZE;
	*len = VAS_HVWC_SIZE;
}

static inline void get_uwc_mmio_bar(struct vas_window *window,
			u64 *start, int *len)
{
	u64 pbaddr;

	pbaddr = window->vinst->uwc_bar_start;
	*start = pbaddr + window->winid * VAS_UWC_SIZE;
	*len = VAS_UWC_SIZE;
}

/*
 * Map the paste bus address of the given send window into kernel address
 * space. Unlike MMIO regions (map_mmio_region() below), paste region must
 * be mapped cache-able and is only applicable to send windows.
 */
static void *map_paste_region(struct vas_window *txwin)
{
	int len;
	void *map;
	char *name;
	u64 start;

	name = kasprintf(GFP_KERNEL, "window-v%d-w%d", txwin->vinst->vas_id,
				txwin->winid);
	if (!name)
		goto free_name;

	txwin->paste_addr_name = name;
	compute_paste_address(txwin, &start, &len);

	if (!request_mem_region(start, len, name)) {
		pr_devel("%s(): request_mem_region(0x%llx, %d) failed\n",
				__func__, start, len);
		goto free_name;
	}

	map = ioremap_cache(start, len);
	if (!map) {
		pr_devel("%s(): ioremap_cache(0x%llx, %d) failed\n", __func__,
				start, len);
		goto free_name;
	}

	pr_devel("Mapped paste addr 0x%llx to kaddr 0x%p\n", start, map);
	return map;

free_name:
	kfree(name);
	return ERR_PTR(-ENOMEM);
}

static void *map_mmio_region(char *name, u64 start, int len)
{
	void *map;

	if (!request_mem_region(start, len, name)) {
		pr_devel("%s(): request_mem_region(0x%llx, %d) failed\n",
				__func__, start, len);
		return NULL;
	}

	map = ioremap(start, len);
	if (!map) {
		pr_devel("%s(): ioremap(0x%llx, %d) failed\n", __func__, start,
				len);
		return NULL;
	}

	return map;
}

static void unmap_region(void *addr, u64 start, int len)
{
	iounmap(addr);
	release_mem_region((phys_addr_t)start, len);
}

/*
 * Unmap the paste address region for a window.
 */
static void unmap_paste_region(struct vas_window *window)
{
	int len;
	u64 busaddr_start;

	if (window->paste_kaddr) {
		compute_paste_address(window, &busaddr_start, &len);
		unmap_region(window->paste_kaddr, busaddr_start, len);
		window->paste_kaddr = NULL;
		kfree(window->paste_addr_name);
		window->paste_addr_name = NULL;
	}
}

/*
 * Unmap the MMIO regions for a window.
 */
static void unmap_winctx_mmio_bars(struct vas_window *window)
{
	int len;
	u64 busaddr_start;

	if (window->hvwc_map) {
		get_hvwc_mmio_bar(window, &busaddr_start, &len);
		unmap_region(window->hvwc_map, busaddr_start, len);
		window->hvwc_map = NULL;
	}

	if (window->uwc_map) {
		get_uwc_mmio_bar(window, &busaddr_start, &len);
		unmap_region(window->uwc_map, busaddr_start, len);
		window->uwc_map = NULL;
	}
}

/*
 * Find the Hypervisor Window Context (HVWC) MMIO Base Address Region and the
 * OS/User Window Context (UWC) MMIO Base Address Region for the given window.
 * Map these bus addresses and save the mapped kernel addresses in @window.
 */
int map_winctx_mmio_bars(struct vas_window *window)
{
	int len;
	u64 start;

	get_hvwc_mmio_bar(window, &start, &len);
	window->hvwc_map = map_mmio_region("HVWCM_Window", start, len);

	get_uwc_mmio_bar(window, &start, &len);
	window->uwc_map = map_mmio_region("UWCM_Window", start, len);

	if (!window->hvwc_map || !window->uwc_map) {
		unmap_winctx_mmio_bars(window);
		return -1;
	}

	return 0;
}

/*
 * Reset all valid registers in the HV and OS/User Window Contexts for
 * the window identified by @window.
 *
 * NOTE: We cannot really use a for loop to reset window context. Not all
 *	 offsets in a window context are valid registers and the valid
 *	 registers are not sequential. And, we can only write to offsets
 *	 with valid registers.
 */
void reset_window_regs(struct vas_window *window)
{
	write_hvwc_reg(window, VREG(LPID), 0ULL);
	write_hvwc_reg(window, VREG(PID), 0ULL);
	write_hvwc_reg(window, VREG(XLATE_MSR), 0ULL);
	write_hvwc_reg(window, VREG(XLATE_LPCR), 0ULL);
	write_hvwc_reg(window, VREG(XLATE_CTL), 0ULL);
	write_hvwc_reg(window, VREG(AMR), 0ULL);
	write_hvwc_reg(window, VREG(SEIDR), 0ULL);
	write_hvwc_reg(window, VREG(FAULT_TX_WIN), 0ULL);
	write_hvwc_reg(window, VREG(OSU_INTR_SRC_RA), 0ULL);
	write_hvwc_reg(window, VREG(HV_INTR_SRC_RA), 0ULL);
	write_hvwc_reg(window, VREG(PSWID), 0ULL);
	write_hvwc_reg(window, VREG(LFIFO_BAR), 0ULL);
	write_hvwc_reg(window, VREG(LDATA_STAMP_CTL), 0ULL);
	write_hvwc_reg(window, VREG(LDMA_CACHE_CTL), 0ULL);
	write_hvwc_reg(window, VREG(LRFIFO_PUSH), 0ULL);
	write_hvwc_reg(window, VREG(CURR_MSG_COUNT), 0ULL);
	write_hvwc_reg(window, VREG(LNOTIFY_AFTER_COUNT), 0ULL);
	write_hvwc_reg(window, VREG(LRX_WCRED), 0ULL);
	write_hvwc_reg(window, VREG(LRX_WCRED_ADDER), 0ULL);
	write_hvwc_reg(window, VREG(TX_WCRED), 0ULL);
	write_hvwc_reg(window, VREG(TX_WCRED_ADDER), 0ULL);
	write_hvwc_reg(window, VREG(LFIFO_SIZE), 0ULL);
	write_hvwc_reg(window, VREG(WINCTL), 0ULL);
	write_hvwc_reg(window, VREG(WIN_STATUS), 0ULL);
	write_hvwc_reg(window, VREG(WIN_CTX_CACHING_CTL), 0ULL);
	write_hvwc_reg(window, VREG(TX_RSVD_BUF_COUNT), 0ULL);
	write_hvwc_reg(window, VREG(LRFIFO_WIN_PTR), 0ULL);
	write_hvwc_reg(window, VREG(LNOTIFY_CTL), 0ULL);
	write_hvwc_reg(window, VREG(LNOTIFY_PID), 0ULL);
	write_hvwc_reg(window, VREG(LNOTIFY_LPID), 0ULL);
	write_hvwc_reg(window, VREG(LNOTIFY_TID), 0ULL);
	write_hvwc_reg(window, VREG(LNOTIFY_SCOPE), 0ULL);
	write_hvwc_reg(window, VREG(NX_UTIL_ADDER), 0ULL);

	/* Skip read-only registers: NX_UTIL and NX_UTIL_SE */

	/*
	 * The send and receive window credit adder registers are also
	 * accessible from HVWC and have been initialized above. We don't
	 * need to initialize from the OS/User Window Context, so skip
	 * following calls:
	 *
	 *	write_uwc_reg(window, VREG(TX_WCRED_ADDER), 0ULL);
	 *	write_uwc_reg(window, VREG(LRX_WCRED_ADDER), 0ULL);
	 */
}

/*
 * Initialize window context registers related to Address Translation.
 * These registers are common to send/receive windows although they
 * differ for user/kernel windows. As we resolve the TODOs we may
 * want to add fields to vas_winctx and move the initialization to
 * init_vas_winctx_regs().
 */
static void init_xlate_regs(struct vas_window *window, bool user_win)
{
	u64 lpcr, val;

	/*
	 * MSR_TA, MSR_US are false for both kernel and user.
	 * MSR_DR and MSR_PR are false for kernel.
	 */
	val = 0ULL;
	val = SET_FIELD(VAS_XLATE_MSR_HV, val, 1);
	val = SET_FIELD(VAS_XLATE_MSR_SF, val, 1);
	if (user_win) {
		val = SET_FIELD(VAS_XLATE_MSR_DR, val, 1);
		val = SET_FIELD(VAS_XLATE_MSR_PR, val, 1);
	}
	write_hvwc_reg(window, VREG(XLATE_MSR), val);

	lpcr = mfspr(SPRN_LPCR);
	val = 0ULL;
	/*
	 * NOTE: From Section 5.7.8.1 Segment Lookaside Buffer of the
	 *	 Power ISA, v3.0B, Page size encoding is 0 = 4KB, 5 = 64KB.
	 *
	 * NOTE: From Section 1.3.1, Address Translation Context of the
	 *	 Nest MMU Workbook, LPCR_SC should be 0 for Power9.
	 */
	val = SET_FIELD(VAS_XLATE_LPCR_PAGE_SIZE, val, 5);
	val = SET_FIELD(VAS_XLATE_LPCR_ISL, val, lpcr & LPCR_ISL);
	val = SET_FIELD(VAS_XLATE_LPCR_TC, val, lpcr & LPCR_TC);
	val = SET_FIELD(VAS_XLATE_LPCR_SC, val, 0);
	write_hvwc_reg(window, VREG(XLATE_LPCR), val);

	/*
	 * Section 1.3.1 (Address translation Context) of NMMU workbook.
	 *	0b00	Hashed Page Table mode
	 *	0b01	Reserved
	 *	0b10	Radix on HPT
	 *	0b11	Radix on Radix
	 */
	val = 0ULL;
	val = SET_FIELD(VAS_XLATE_MODE, val, radix_enabled() ? 3 : 2);
	write_hvwc_reg(window, VREG(XLATE_CTL), val);

	/*
	 * TODO: Can we mfspr(AMR) even for user windows?
	 */
	val = 0ULL;
	val = SET_FIELD(VAS_AMR, val, mfspr(SPRN_AMR));
	write_hvwc_reg(window, VREG(AMR), val);

	val = 0ULL;
	val = SET_FIELD(VAS_SEIDR, val, 0);
	write_hvwc_reg(window, VREG(SEIDR), val);
}

/*
 * Initialize Reserved Send Buffer Count for the send window. It involves
 * writing to the register, reading it back to confirm that the hardware
 * has enough buffers to reserve. See section 1.3.1.2.1 of VAS workbook.
 *
 * Since we can only make a best-effort attempt to fulfill the request,
 * we don't return any errors if we cannot.
 *
 * TODO: Reserved (aka dedicated) send buffers are not supported yet.
 */
static void init_rsvd_tx_buf_count(struct vas_window *txwin,
				struct vas_winctx *winctx)
{
	write_hvwc_reg(txwin, VREG(TX_RSVD_BUF_COUNT), 0ULL);
}

/*
 * init_winctx_regs()
 *	Initialize window context registers for a receive window.
 *	Except for caching control and marking window open, the registers
 *	are initialized in the order listed in Section 3.1.4 (Window Context
 *	Cache Register Details) of the VAS workbook although they don't need
 *	to be.
 *
 * Design note: For NX receive windows, NX allocates the FIFO buffer in OPAL
 *	(so that it can get a large contiguous area) and passes that buffer
 *	to kernel via device tree. We now write that buffer address to the
 *	FIFO BAR. Would it make sense to do this all in OPAL? i.e have OPAL
 *	write the per-chip RX FIFO addresses to the windows during boot-up
 *	as a one-time task? That could work for NX but what about other
 *	receivers?  Let the receivers tell us the rx-fifo buffers for now.
 */
int init_winctx_regs(struct vas_window *window, struct vas_winctx *winctx)
{
	u64 val;
	int fifo_size;

	reset_window_regs(window);

	val = 0ULL;
	val = SET_FIELD(VAS_LPID, val, winctx->lpid);
	write_hvwc_reg(window, VREG(LPID), val);

	val = 0ULL;
	val = SET_FIELD(VAS_PID_ID, val, winctx->pidr);
	write_hvwc_reg(window, VREG(PID), val);

	init_xlate_regs(window, winctx->user_win);

	val = 0ULL;
	val = SET_FIELD(VAS_FAULT_TX_WIN, val, 0);
	write_hvwc_reg(window, VREG(FAULT_TX_WIN), val);

	/* In PowerNV, interrupts go to HV. */
	write_hvwc_reg(window, VREG(OSU_INTR_SRC_RA), 0ULL);

	val = 0ULL;
	val = SET_FIELD(VAS_HV_INTR_SRC_RA, val, winctx->irq_port);
	write_hvwc_reg(window, VREG(HV_INTR_SRC_RA), val);

	val = 0ULL;
	val = SET_FIELD(VAS_PSWID_EA_HANDLE, val, winctx->pswid);
	write_hvwc_reg(window, VREG(PSWID), val);

	write_hvwc_reg(window, VREG(SPARE1), 0ULL);
	write_hvwc_reg(window, VREG(SPARE2), 0ULL);
	write_hvwc_reg(window, VREG(SPARE3), 0ULL);

	/*
	 * NOTE: VAS expects the FIFO address to be copied into the LFIFO_BAR
	 *	 register as is - do NOT shift the address into VAS_LFIFO_BAR
	 *	 bit fields! Ok to set the page migration select fields -
	 *	 VAS ignores the lower 10+ bits in the address anyway, because
	 *	 the minimum FIFO size is 1K?
	 *
	 * See also: Design note in function header.
	 */
	val = __pa(winctx->rx_fifo);
	val = SET_FIELD(VAS_PAGE_MIGRATION_SELECT, val, 0);
	write_hvwc_reg(window, VREG(LFIFO_BAR), val);

	val = 0ULL;
	val = SET_FIELD(VAS_LDATA_STAMP, val, winctx->data_stamp);
	write_hvwc_reg(window, VREG(LDATA_STAMP_CTL), val);

	val = 0ULL;
	val = SET_FIELD(VAS_LDMA_TYPE, val, winctx->dma_type);
	val = SET_FIELD(VAS_LDMA_FIFO_DISABLE, val, winctx->fifo_disable);
	write_hvwc_reg(window, VREG(LDMA_CACHE_CTL), val);

	write_hvwc_reg(window, VREG(LRFIFO_PUSH), 0ULL);
	write_hvwc_reg(window, VREG(CURR_MSG_COUNT), 0ULL);
	write_hvwc_reg(window, VREG(LNOTIFY_AFTER_COUNT), 0ULL);

	val = 0ULL;
	val = SET_FIELD(VAS_LRX_WCRED, val, winctx->wcreds_max);
	write_hvwc_reg(window, VREG(LRX_WCRED), val);

	val = 0ULL;
	val = SET_FIELD(VAS_TX_WCRED, val, winctx->wcreds_max);
	write_hvwc_reg(window, VREG(TX_WCRED), val);

	write_hvwc_reg(window, VREG(LRX_WCRED_ADDER), 0ULL);
	write_hvwc_reg(window, VREG(TX_WCRED_ADDER), 0ULL);

	fifo_size = winctx->rx_fifo_size / 1024;

	val = 0ULL;
	val = SET_FIELD(VAS_LFIFO_SIZE, val, ilog2(fifo_size));
	write_hvwc_reg(window, VREG(LFIFO_SIZE), val);

	/* Update window control and caching control registers last so
	 * we mark the window open only after fully initializing it and
	 * pushing context to cache.
	 */

	write_hvwc_reg(window, VREG(WIN_STATUS), 0ULL);

	init_rsvd_tx_buf_count(window, winctx);

	/* for a send window, point to the matching receive window */
	val = 0ULL;
	val = SET_FIELD(VAS_LRX_WIN_ID, val, winctx->rx_win_id);
	write_hvwc_reg(window, VREG(LRFIFO_WIN_PTR), val);

	write_hvwc_reg(window, VREG(SPARE4), 0ULL);

	val = 0ULL;
	val = SET_FIELD(VAS_NOTIFY_DISABLE, val, winctx->notify_disable);
	val = SET_FIELD(VAS_INTR_DISABLE, val, winctx->intr_disable);
	val = SET_FIELD(VAS_NOTIFY_EARLY, val, winctx->notify_early);
	val = SET_FIELD(VAS_NOTIFY_OSU_INTR, val, winctx->notify_os_intr_reg);
	write_hvwc_reg(window, VREG(LNOTIFY_CTL), val);

	val = 0ULL;
	val = SET_FIELD(VAS_LNOTIFY_PID, val, winctx->lnotify_pid);
	write_hvwc_reg(window, VREG(LNOTIFY_PID), val);

	val = 0ULL;
	val = SET_FIELD(VAS_LNOTIFY_LPID, val, winctx->lnotify_lpid);
	write_hvwc_reg(window, VREG(LNOTIFY_LPID), val);

	val = 0ULL;
	val = SET_FIELD(VAS_LNOTIFY_TID, val, winctx->lnotify_tid);
	write_hvwc_reg(window, VREG(LNOTIFY_TID), val);

	val = 0ULL;
	val = SET_FIELD(VAS_LNOTIFY_MIN_SCOPE, val, winctx->min_scope);
	val = SET_FIELD(VAS_LNOTIFY_MAX_SCOPE, val, winctx->max_scope);
	write_hvwc_reg(window, VREG(LNOTIFY_SCOPE), val);

	/* Skip read-only registers NX_UTIL and NX_UTIL_SE */

	write_hvwc_reg(window, VREG(SPARE5), 0ULL);
	write_hvwc_reg(window, VREG(NX_UTIL_ADDER), 0ULL);
	write_hvwc_reg(window, VREG(SPARE6), 0ULL);

	/* Finally, push window context to memory and... */
	val = 0ULL;
	val = SET_FIELD(VAS_PUSH_TO_MEM, val, 1);
	write_hvwc_reg(window, VREG(WIN_CTX_CACHING_CTL), val);

	/* ... mark the window open for business */
	val = 0ULL;
	val = SET_FIELD(VAS_WINCTL_REJ_NO_CREDIT, val, winctx->rej_no_credit);
	val = SET_FIELD(VAS_WINCTL_PIN, val, winctx->pin_win);
	val = SET_FIELD(VAS_WINCTL_TX_WCRED_MODE, val, winctx->tx_wcred_mode);
	val = SET_FIELD(VAS_WINCTL_RX_WCRED_MODE, val, winctx->rx_wcred_mode);
	val = SET_FIELD(VAS_WINCTL_TX_WORD_MODE, val, winctx->tx_word_mode);
	val = SET_FIELD(VAS_WINCTL_RX_WORD_MODE, val, winctx->rx_word_mode);
	val = SET_FIELD(VAS_WINCTL_FAULT_WIN, val, winctx->fault_win);
	val = SET_FIELD(VAS_WINCTL_NX_WIN, val, winctx->nx_win);
	val = SET_FIELD(VAS_WINCTL_OPEN, val, 1);
	write_hvwc_reg(window, VREG(WINCTL), val);

	return 0;
}

static DEFINE_SPINLOCK(vas_ida_lock);

static void vas_release_window_id(struct ida *ida, int winid)
{
	spin_lock(&vas_ida_lock);
	ida_remove(ida, winid);
	spin_unlock(&vas_ida_lock);
}

static int vas_assign_window_id(struct ida *ida)
{
	int rc, winid;

	do {
		rc = ida_pre_get(ida, GFP_KERNEL);
		if (!rc)
			return -EAGAIN;

		spin_lock(&vas_ida_lock);
		rc = ida_get_new(ida, &winid);
		spin_unlock(&vas_ida_lock);
	} while (rc == -EAGAIN);

	if (rc)
		return rc;

	if (winid > VAS_WINDOWS_PER_CHIP) {
		pr_err("Too many (%d) open windows\n", winid);
		vas_release_window_id(ida, winid);
		return -EAGAIN;
	}

	return winid;
}

static void vas_window_free(struct vas_window *window)
{
	int winid = window->winid;
	struct vas_instance *vinst = window->vinst;

	unmap_winctx_mmio_bars(window);
	kfree(window);

	vas_release_window_id(&vinst->ida, winid);
}

static struct vas_window *vas_window_alloc(struct vas_instance *vinst)
{
	int winid;
	struct vas_window *window;

	winid = vas_assign_window_id(&vinst->ida);
	if (winid < 0)
		return ERR_PTR(winid);

	window = kzalloc(sizeof(*window), GFP_KERNEL);
	if (!window)
		goto out_free;

	window->vinst = vinst;
	window->winid = winid;

	if (map_winctx_mmio_bars(window))
		goto out_free;

	return window;

out_free:
	kfree(window);
	vas_release_window_id(&vinst->ida, winid);
	return ERR_PTR(-ENOMEM);
}

static void put_rx_win(struct vas_window *rxwin)
{
	/* Better not be a send window! */
	WARN_ON_ONCE(rxwin->tx_win);

	atomic_dec(&rxwin->num_txwins);
}

/*
 * Get the VAS receive window associated with NX engine identified
 * by @cop and if applicable, @pswid.
 *
 * See also function header of set_vinst_win().
 */
static struct vas_window *get_vinst_rxwin(struct vas_instance *vinst,
			enum vas_cop_type cop, u32 pswid)
{
	struct vas_window *rxwin;

	mutex_lock(&vinst->mutex);

	if (cop == VAS_COP_TYPE_842 || cop == VAS_COP_TYPE_842_HIPRI)
		rxwin = vinst->rxwin[cop] ?: ERR_PTR(-EINVAL);
	else
		rxwin = ERR_PTR(-EINVAL);

	if (!IS_ERR(rxwin))
		atomic_inc(&rxwin->num_txwins);

	mutex_unlock(&vinst->mutex);

	return rxwin;
}

/*
 * We have two tables of windows in a VAS instance. The first one,
 * ->windows[], contains all the windows in the instance and allows
 * looking up a window by its id. It is used to look up send windows
 * during fault handling and receive windows when pairing user space
 * send/receive windows.
 *
 * The second table, ->rxwin[], contains receive windows that are
 * associated with NX engines. This table has VAS_COP_TYPE_MAX
 * entries and is used to look up a receive window by its
 * coprocessor type.
 *
 * Here, we save @window in the ->windows[] table. If it is a receive
 * window, we also save the window in the ->rxwin[] table.
 */
static void set_vinst_win(struct vas_instance *vinst,
			struct vas_window *window)
{
	int id = window->winid;

	mutex_lock(&vinst->mutex);

	/*
	 * There should only be one receive window for a coprocessor type
	 * unless its a user (FTW) window.
	 */
	if (!window->user_win && !window->tx_win) {
		WARN_ON_ONCE(vinst->rxwin[window->cop]);
		vinst->rxwin[window->cop] = window;
	}

	WARN_ON_ONCE(vinst->windows[id] != NULL);
	vinst->windows[id] = window;

	mutex_unlock(&vinst->mutex);
}

/*
 * Clear this window from the table(s) of windows for this VAS instance.
 * See also function header of set_vinst_win().
 */
static void clear_vinst_win(struct vas_window *window)
{
	int id = window->winid;
	struct vas_instance *vinst = window->vinst;

	mutex_lock(&vinst->mutex);

	if (!window->user_win && !window->tx_win) {
		WARN_ON_ONCE(!vinst->rxwin[window->cop]);
		vinst->rxwin[window->cop] = NULL;
	}

	WARN_ON_ONCE(vinst->windows[id] != window);
	vinst->windows[id] = NULL;

	mutex_unlock(&vinst->mutex);
}

static void init_winctx_for_rxwin(struct vas_window *rxwin,
			struct vas_rx_win_attr *rxattr,
			struct vas_winctx *winctx)
{
	/*
	 * We first zero (memset()) all fields and only set non-zero fields.
	 * Following fields are 0/false but maybe deserve a comment:
	 *
	 *	->notify_os_intr_reg	In powerNV, send intrs to HV
	 *	->notify_disable	False for NX windows
	 *	->intr_disable		False for Fault Windows
	 *	->xtra_write		False for NX windows
	 *	->notify_early		NA for NX windows
	 *	->rsvd_txbuf_count	NA for Rx windows
	 *	->lpid, ->pid, ->tid	NA for Rx windows
	 */

	memset(winctx, 0, sizeof(struct vas_winctx));

	winctx->rx_fifo = rxattr->rx_fifo;
	winctx->rx_fifo_size = rxattr->rx_fifo_size;
	winctx->wcreds_max = rxattr->wcreds_max ?: VAS_WCREDS_DEFAULT;
	winctx->pin_win = rxattr->pin_win;

	winctx->nx_win = rxattr->nx_win;
	winctx->fault_win = rxattr->fault_win;
	winctx->user_win = rxattr->user_win;
	winctx->rej_no_credit = rxattr->rej_no_credit;
	winctx->rx_word_mode = rxattr->rx_win_ord_mode;
	winctx->tx_word_mode = rxattr->tx_win_ord_mode;
	winctx->rx_wcred_mode = rxattr->rx_wcred_mode;
	winctx->tx_wcred_mode = rxattr->tx_wcred_mode;
	winctx->notify_early = rxattr->notify_early;

	if (winctx->nx_win) {
		winctx->data_stamp = true;
		winctx->intr_disable = true;
		winctx->pin_win = true;

		WARN_ON_ONCE(winctx->fault_win);
		WARN_ON_ONCE(!winctx->rx_word_mode);
		WARN_ON_ONCE(!winctx->tx_word_mode);
		WARN_ON_ONCE(winctx->notify_after_count);
	} else if (winctx->fault_win) {
		winctx->notify_disable = true;
	} else if (winctx->user_win) {
		/*
		 * Section 1.8.1 Low Latency Core-Core Wake up of
		 * the VAS workbook:
		 *
		 *      - disable credit checks ([tr]x_wcred_mode = false)
		 *      - disable FIFO writes
		 *      - enable ASB_Notify, disable interrupt
		 */
		winctx->fifo_disable = true;
		winctx->intr_disable = true;
		winctx->rx_fifo = NULL;
	}

	winctx->lnotify_lpid = rxattr->lnotify_lpid;
	winctx->lnotify_pid = rxattr->lnotify_pid;
	winctx->lnotify_tid = rxattr->lnotify_tid;
	winctx->pswid = rxattr->pswid;
	winctx->dma_type = VAS_DMA_TYPE_INJECT;
	winctx->tc_mode = rxattr->tc_mode;

	winctx->min_scope = VAS_SCOPE_LOCAL;
	winctx->max_scope = VAS_SCOPE_VECTORED_GROUP;
}

static bool rx_win_args_valid(enum vas_cop_type cop,
			struct vas_rx_win_attr *attr)
{
	dump_rx_win_attr(attr);

	if (cop >= VAS_COP_TYPE_MAX)
		return false;

	if (cop != VAS_COP_TYPE_FTW &&
				attr->rx_fifo_size < VAS_RX_FIFO_SIZE_MIN)
		return false;

	if (attr->rx_fifo_size > VAS_RX_FIFO_SIZE_MAX)
		return false;

	if (attr->nx_win) {
		/* cannot be fault or user window if it is nx */
		if (attr->fault_win || attr->user_win)
			return false;
		/*
		 * Section 3.1.4.32: NX Windows must not disable notification,
		 *	and must not enable interrupts or early notification.
		 */
		if (attr->notify_disable || !attr->intr_disable ||
				attr->notify_early)
			return false;
	} else if (attr->fault_win) {
		/* cannot be both fault and user window */
		if (attr->user_win)
			return false;

		/*
		 * Section 3.1.4.32: Fault windows must disable notification
		 *	but not interrupts.
		 */
		if (!attr->notify_disable || attr->intr_disable)
			return false;

	} else if (attr->user_win) {
		/*
		 * User receive windows are only for fast-thread-wakeup
		 * (FTW). They don't need a FIFO and must disable interrupts
		 */
		if (attr->rx_fifo || attr->rx_fifo_size || !attr->intr_disable)
			return false;
	} else {
		/* Rx window must be one of NX or Fault or User window. */
		return false;
	}

	return true;
}

void vas_init_rx_win_attr(struct vas_rx_win_attr *rxattr, enum vas_cop_type cop)
{
	memset(rxattr, 0, sizeof(*rxattr));

	if (cop == VAS_COP_TYPE_842 || cop == VAS_COP_TYPE_842_HIPRI) {
		rxattr->pin_win = true;
		rxattr->nx_win = true;
		rxattr->fault_win = false;
		rxattr->intr_disable = true;
		rxattr->rx_wcred_mode = true;
		rxattr->tx_wcred_mode = true;
		rxattr->rx_win_ord_mode = true;
		rxattr->tx_win_ord_mode = true;
	} else if (cop == VAS_COP_TYPE_FAULT) {
		rxattr->pin_win = true;
		rxattr->fault_win = true;
		rxattr->notify_disable = true;
		rxattr->rx_wcred_mode = true;
		rxattr->tx_wcred_mode = true;
		rxattr->rx_win_ord_mode = true;
		rxattr->tx_win_ord_mode = true;
	} else if (cop == VAS_COP_TYPE_FTW) {
		rxattr->user_win = true;
		rxattr->intr_disable = true;

		/*
		 * As noted in the VAS Workbook we disable credit checks.
		 * If we enable credit checks in the future, we must also
		 * implement a mechanism to return the user credits or new
		 * paste operations will fail.
		 */
	}
}
EXPORT_SYMBOL_GPL(vas_init_rx_win_attr);

struct vas_window *vas_rx_win_open(int vasid, enum vas_cop_type cop,
			struct vas_rx_win_attr *rxattr)
{
	struct vas_window *rxwin;
	struct vas_winctx winctx;
	struct vas_instance *vinst;

	if (!rx_win_args_valid(cop, rxattr))
		return ERR_PTR(-EINVAL);

	vinst = find_vas_instance(vasid);
	if (!vinst) {
		pr_devel("vasid %d not found!\n", vasid);
		return ERR_PTR(-EINVAL);
	}
	pr_devel("Found instance %d\n", vasid);

	rxwin = vas_window_alloc(vinst);
	if (IS_ERR(rxwin)) {
		pr_devel("Unable to allocate memory for Rx window\n");
		return rxwin;
	}

	rxwin->tx_win = false;
	rxwin->nx_win = rxattr->nx_win;
	rxwin->user_win = rxattr->user_win;
	rxwin->cop = cop;
	if (rxattr->user_win)
		rxwin->pid = task_pid_vnr(current);

	init_winctx_for_rxwin(rxwin, rxattr, &winctx);
	init_winctx_regs(rxwin, &winctx);

	set_vinst_win(vinst, rxwin);

	return rxwin;
}
EXPORT_SYMBOL_GPL(vas_rx_win_open);

void vas_init_tx_win_attr(struct vas_tx_win_attr *txattr, enum vas_cop_type cop)
{
	memset(txattr, 0, sizeof(*txattr));

	if (cop == VAS_COP_TYPE_842 || cop == VAS_COP_TYPE_842_HIPRI) {
		txattr->rej_no_credit = false;
		txattr->rx_wcred_mode = true;
		txattr->tx_wcred_mode = true;
		txattr->rx_win_ord_mode = true;
		txattr->tx_win_ord_mode = true;
	} else if (cop == VAS_COP_TYPE_FTW) {
		txattr->user_win = true;
	}
}
EXPORT_SYMBOL_GPL(vas_init_tx_win_attr);

static void init_winctx_for_txwin(struct vas_window *txwin,
			struct vas_tx_win_attr *txattr,
			struct vas_winctx *winctx)
{
	/*
	 * We first zero all fields and only set non-zero ones. Following
	 * are some fields set to 0/false for the stated reason:
	 *
	 *	->notify_os_intr_reg	In powernv, send intrs to HV
	 *	->rsvd_txbuf_count	Not supported yet.
	 *	->notify_disable	False for NX windows
	 *	->xtra_write		False for NX windows
	 *	->notify_early		NA for NX windows
	 *	->lnotify_lpid		NA for Tx windows
	 *	->lnotify_pid		NA for Tx windows
	 *	->lnotify_tid		NA for Tx windows
	 *	->tx_win_cred_mode	Ignore for now for NX windows
	 *	->rx_win_cred_mode	Ignore for now for NX windows
	 */
	memset(winctx, 0, sizeof(struct vas_winctx));

	winctx->wcreds_max = txattr->wcreds_max ?: VAS_WCREDS_DEFAULT;

	winctx->user_win = txattr->user_win;
	winctx->nx_win = txwin->rxwin->nx_win;
	winctx->pin_win = txattr->pin_win;
	winctx->rej_no_credit = txattr->rej_no_credit;
	winctx->rsvd_txbuf_enable = txattr->rsvd_txbuf_enable;

	winctx->rx_wcred_mode = txattr->rx_wcred_mode;
	winctx->tx_wcred_mode = txattr->tx_wcred_mode;
	winctx->rx_word_mode = txattr->rx_win_ord_mode;
	winctx->tx_word_mode = txattr->tx_win_ord_mode;
	winctx->rsvd_txbuf_count = txattr->rsvd_txbuf_count;

	if (winctx->nx_win) {
		winctx->data_stamp = true;
		winctx->intr_disable = true;
	}

	winctx->lpid = txattr->lpid;
	winctx->pidr = txattr->pidr;
	winctx->rx_win_id = txwin->rxwin->winid;

	winctx->dma_type = VAS_DMA_TYPE_INJECT;
	winctx->tc_mode = txattr->tc_mode;
	winctx->min_scope = VAS_SCOPE_LOCAL;
	winctx->max_scope = VAS_SCOPE_VECTORED_GROUP;

	winctx->pswid = 0;
}

static bool tx_win_args_valid(enum vas_cop_type cop,
			struct vas_tx_win_attr *attr)
{
	if (attr->tc_mode != VAS_THRESH_DISABLED)
		return false;

	if (cop > VAS_COP_TYPE_MAX)
		return false;

	if (attr->user_win &&
			(cop != VAS_COP_TYPE_FTW || attr->rsvd_txbuf_count))
		return false;

	return true;
}

struct vas_window *vas_tx_win_open(int vasid, enum vas_cop_type cop,
			struct vas_tx_win_attr *attr)
{
	int rc;
	struct vas_window *txwin;
	struct vas_window *rxwin;
	struct vas_winctx winctx;
	struct vas_instance *vinst;

	if (!tx_win_args_valid(cop, attr))
		return ERR_PTR(-EINVAL);

	vinst = find_vas_instance(vasid);
	if (!vinst) {
		pr_devel("vasid %d not found!\n", vasid);
		return ERR_PTR(-EINVAL);
	}

	rxwin = get_vinst_rxwin(vinst, cop, attr->pswid);
	if (IS_ERR(rxwin)) {
		pr_devel("No RxWin for vasid %d, cop %d\n", vasid, cop);
		return rxwin;
	}

	txwin = vas_window_alloc(vinst);
	if (IS_ERR(txwin)) {
		rc = PTR_ERR(txwin);
		goto put_rxwin;
	}

	txwin->tx_win = 1;
	txwin->rxwin = rxwin;
	txwin->nx_win = txwin->rxwin->nx_win;
	txwin->pid = attr->pid;
	txwin->user_win = attr->user_win;

	init_winctx_for_txwin(txwin, attr, &winctx);

	init_winctx_regs(txwin, &winctx);

	/*
	 * If its a kernel send window, map the window address into the
	 * kernel's address space. For user windows, user must issue an
	 * mmap() to map the window into their address space.
	 *
	 * NOTE: If kernel ever resubmits a user CRB after handling a page
	 *	 fault, we will need to map this into kernel as well.
	 */
	if (!txwin->user_win) {
		txwin->paste_kaddr = map_paste_region(txwin);
		if (IS_ERR(txwin->paste_kaddr)) {
			rc = PTR_ERR(txwin->paste_kaddr);
			goto free_window;
		}
	}

	set_vinst_win(vinst, txwin);

	return txwin;

free_window:
	vas_window_free(txwin);

put_rxwin:
	put_rx_win(rxwin);
	return ERR_PTR(rc);

}
EXPORT_SYMBOL_GPL(vas_tx_win_open);

int vas_copy_crb(void *crb, int offset)
{
	return vas_copy(crb, offset);
}
EXPORT_SYMBOL_GPL(vas_copy_crb);

#define RMA_LSMP_REPORT_ENABLE PPC_BIT(53)
int vas_paste_crb(struct vas_window *txwin, int offset, bool re)
{
	int rc;
	void *addr;
	uint64_t val;

	/*
	 * Only NX windows are supported for now and hardware assumes
	 * report-enable flag is set for NX windows. Ensure software
	 * complies too.
	 */
	WARN_ON_ONCE(txwin->nx_win && !re);

	addr = txwin->paste_kaddr;
	if (re) {
		/*
		 * Set the REPORT_ENABLE bit (equivalent to writing
		 * to 1K offset of the paste address)
		 */
		val = SET_FIELD(RMA_LSMP_REPORT_ENABLE, 0ULL, 1);
		addr += val;
	}

	/*
	 * Map the raw CR value from vas_paste() to an error code (there
	 * is just pass or fail for now though).
	 */
	rc = vas_paste(addr, offset);
	if (rc == 2)
		rc = 0;
	else
		rc = -EINVAL;

	print_fifo_msg_count(txwin);

	return rc;
}
EXPORT_SYMBOL_GPL(vas_paste_crb);

static void poll_window_busy_state(struct vas_window *window)
{
	int busy;
	u64 val;

retry:
	/*
	 * Poll Window Busy flag
	 */
	val = read_hvwc_reg(window, VREG(WIN_STATUS));
	busy = GET_FIELD(VAS_WIN_BUSY, val);
	if (busy) {
		val = 0;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ);
		goto retry;
	}
}

static void poll_window_castout(struct vas_window *window)
{
	int cached;
	u64 val;

	/* Cast window context out of the cache */
retry:
	val = read_hvwc_reg(window, VREG(WIN_CTX_CACHING_CTL));
	cached = GET_FIELD(VAS_WIN_CACHE_STATUS, val);
	if (cached) {
		val = 0ULL;
		val = SET_FIELD(VAS_CASTOUT_REQ, val, 1);
		val = SET_FIELD(VAS_PUSH_TO_MEM, val, 0);
		write_hvwc_reg(window, VREG(WIN_CTX_CACHING_CTL), val);

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ);
		goto retry;
	}
}

/*
 * Close a window.
 *
 * See Section 1.12.1 of VAS workbook v1.05 for details on closing window:
 *	- Disable new paste operations (unmap paste address)
 *	- Poll for the "Window Busy" bit to be cleared
 *	- Clear the Open/Enable bit for the Window.
 *	- Poll for return of window Credits (implies FIFO empty for Rx win?)
 *	- Unpin and cast window context out of cache
 *
 * Besides the hardware, kernel has some bookkeeping of course.
 */
int vas_win_close(struct vas_window *window)
{
	u64 val;

	if (!window)
		return 0;

	if (!window->tx_win && atomic_read(&window->num_txwins) != 0) {
		pr_devel("Attempting to close an active Rx window!\n");
		WARN_ON_ONCE(1);
		return -EBUSY;
	}

	unmap_paste_region(window);

	clear_vinst_win(window);

	poll_window_busy_state(window);

	/* Unpin window from cache and close it */
	val = read_hvwc_reg(window, VREG(WINCTL));
	val = SET_FIELD(VAS_WINCTL_PIN, val, 0);
	val = SET_FIELD(VAS_WINCTL_OPEN, val, 0);
	write_hvwc_reg(window, VREG(WINCTL), val);

	poll_window_castout(window);

	/* if send window, drop reference to matching receive window */
	if (window->tx_win)
		put_rx_win(window->rxwin);

	vas_window_free(window);

	return 0;
}
EXPORT_SYMBOL_GPL(vas_win_close);
