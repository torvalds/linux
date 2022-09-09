// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2016-17 IBM Corp.
 */

#define pr_fmt(fmt) "vas: " fmt

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <linux/rcupdate.h>
#include <linux/cred.h>
#include <linux/sched/mm.h>
#include <linux/mmu_context.h>
#include <asm/switch_to.h>
#include <asm/ppc-opcode.h>
#include <asm/vas.h>
#include "vas.h"
#include "copy-paste.h"

#define CREATE_TRACE_POINTS
#include "vas-trace.h"

/*
 * Compute the paste address region for the window @window using the
 * ->paste_base_addr and ->paste_win_id_shift we got from device tree.
 */
void vas_win_paste_addr(struct pnv_vas_window *window, u64 *addr, int *len)
{
	int winid;
	u64 base, shift;

	base = window->vinst->paste_base_addr;
	shift = window->vinst->paste_win_id_shift;
	winid = window->vas_win.winid;

	*addr  = base + (winid << shift);
	if (len)
		*len = PAGE_SIZE;

	pr_debug("Txwin #%d: Paste addr 0x%llx\n", winid, *addr);
}

static inline void get_hvwc_mmio_bar(struct pnv_vas_window *window,
			u64 *start, int *len)
{
	u64 pbaddr;

	pbaddr = window->vinst->hvwc_bar_start;
	*start = pbaddr + window->vas_win.winid * VAS_HVWC_SIZE;
	*len = VAS_HVWC_SIZE;
}

static inline void get_uwc_mmio_bar(struct pnv_vas_window *window,
			u64 *start, int *len)
{
	u64 pbaddr;

	pbaddr = window->vinst->uwc_bar_start;
	*start = pbaddr + window->vas_win.winid * VAS_UWC_SIZE;
	*len = VAS_UWC_SIZE;
}

/*
 * Map the paste bus address of the given send window into kernel address
 * space. Unlike MMIO regions (map_mmio_region() below), paste region must
 * be mapped cache-able and is only applicable to send windows.
 */
static void *map_paste_region(struct pnv_vas_window *txwin)
{
	int len;
	void *map;
	char *name;
	u64 start;

	name = kasprintf(GFP_KERNEL, "window-v%d-w%d", txwin->vinst->vas_id,
				txwin->vas_win.winid);
	if (!name)
		goto free_name;

	txwin->paste_addr_name = name;
	vas_win_paste_addr(txwin, &start, &len);

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
static void unmap_paste_region(struct pnv_vas_window *window)
{
	int len;
	u64 busaddr_start;

	if (window->paste_kaddr) {
		vas_win_paste_addr(window, &busaddr_start, &len);
		unmap_region(window->paste_kaddr, busaddr_start, len);
		window->paste_kaddr = NULL;
		kfree(window->paste_addr_name);
		window->paste_addr_name = NULL;
	}
}

/*
 * Unmap the MMIO regions for a window. Hold the vas_mutex so we don't
 * unmap when the window's debugfs dir is in use. This serializes close
 * of a window even on another VAS instance but since its not a critical
 * path, just minimize the time we hold the mutex for now. We can add
 * a per-instance mutex later if necessary.
 */
static void unmap_winctx_mmio_bars(struct pnv_vas_window *window)
{
	int len;
	void *uwc_map;
	void *hvwc_map;
	u64 busaddr_start;

	mutex_lock(&vas_mutex);

	hvwc_map = window->hvwc_map;
	window->hvwc_map = NULL;

	uwc_map = window->uwc_map;
	window->uwc_map = NULL;

	mutex_unlock(&vas_mutex);

	if (hvwc_map) {
		get_hvwc_mmio_bar(window, &busaddr_start, &len);
		unmap_region(hvwc_map, busaddr_start, len);
	}

	if (uwc_map) {
		get_uwc_mmio_bar(window, &busaddr_start, &len);
		unmap_region(uwc_map, busaddr_start, len);
	}
}

/*
 * Find the Hypervisor Window Context (HVWC) MMIO Base Address Region and the
 * OS/User Window Context (UWC) MMIO Base Address Region for the given window.
 * Map these bus addresses and save the mapped kernel addresses in @window.
 */
static int map_winctx_mmio_bars(struct pnv_vas_window *window)
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
static void reset_window_regs(struct pnv_vas_window *window)
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
static void init_xlate_regs(struct pnv_vas_window *window, bool user_win)
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
static void init_rsvd_tx_buf_count(struct pnv_vas_window *txwin,
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
static void init_winctx_regs(struct pnv_vas_window *window,
			     struct vas_winctx *winctx)
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
	val = SET_FIELD(VAS_FAULT_TX_WIN, val, winctx->fault_win_id);
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
	val = winctx->rx_fifo;
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
}

static void vas_release_window_id(struct ida *ida, int winid)
{
	ida_free(ida, winid);
}

static int vas_assign_window_id(struct ida *ida)
{
	int winid = ida_alloc_max(ida, VAS_WINDOWS_PER_CHIP - 1, GFP_KERNEL);

	if (winid == -ENOSPC) {
		pr_err("Too many (%d) open windows\n", VAS_WINDOWS_PER_CHIP);
		return -EAGAIN;
	}

	return winid;
}

static void vas_window_free(struct pnv_vas_window *window)
{
	struct vas_instance *vinst = window->vinst;
	int winid = window->vas_win.winid;

	unmap_winctx_mmio_bars(window);

	vas_window_free_dbgdir(window);

	kfree(window);

	vas_release_window_id(&vinst->ida, winid);
}

static struct pnv_vas_window *vas_window_alloc(struct vas_instance *vinst)
{
	int winid;
	struct pnv_vas_window *window;

	winid = vas_assign_window_id(&vinst->ida);
	if (winid < 0)
		return ERR_PTR(winid);

	window = kzalloc(sizeof(*window), GFP_KERNEL);
	if (!window)
		goto out_free;

	window->vinst = vinst;
	window->vas_win.winid = winid;

	if (map_winctx_mmio_bars(window))
		goto out_free;

	vas_window_init_dbgdir(window);

	return window;

out_free:
	kfree(window);
	vas_release_window_id(&vinst->ida, winid);
	return ERR_PTR(-ENOMEM);
}

static void put_rx_win(struct pnv_vas_window *rxwin)
{
	/* Better not be a send window! */
	WARN_ON_ONCE(rxwin->tx_win);

	atomic_dec(&rxwin->num_txwins);
}

/*
 * Find the user space receive window given the @pswid.
 *      - We must have a valid vasid and it must belong to this instance.
 *        (so both send and receive windows are on the same VAS instance)
 *      - The window must refer to an OPEN, FTW, RECEIVE window.
 *
 * NOTE: We access ->windows[] table and assume that vinst->mutex is held.
 */
static struct pnv_vas_window *get_user_rxwin(struct vas_instance *vinst,
					     u32 pswid)
{
	int vasid, winid;
	struct pnv_vas_window *rxwin;

	decode_pswid(pswid, &vasid, &winid);

	if (vinst->vas_id != vasid)
		return ERR_PTR(-EINVAL);

	rxwin = vinst->windows[winid];

	if (!rxwin || rxwin->tx_win || rxwin->vas_win.cop != VAS_COP_TYPE_FTW)
		return ERR_PTR(-EINVAL);

	return rxwin;
}

/*
 * Get the VAS receive window associated with NX engine identified
 * by @cop and if applicable, @pswid.
 *
 * See also function header of set_vinst_win().
 */
static struct pnv_vas_window *get_vinst_rxwin(struct vas_instance *vinst,
			enum vas_cop_type cop, u32 pswid)
{
	struct pnv_vas_window *rxwin;

	mutex_lock(&vinst->mutex);

	if (cop == VAS_COP_TYPE_FTW)
		rxwin = get_user_rxwin(vinst, pswid);
	else
		rxwin = vinst->rxwin[cop] ?: ERR_PTR(-EINVAL);

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
			struct pnv_vas_window *window)
{
	int id = window->vas_win.winid;

	mutex_lock(&vinst->mutex);

	/*
	 * There should only be one receive window for a coprocessor type
	 * unless its a user (FTW) window.
	 */
	if (!window->user_win && !window->tx_win) {
		WARN_ON_ONCE(vinst->rxwin[window->vas_win.cop]);
		vinst->rxwin[window->vas_win.cop] = window;
	}

	WARN_ON_ONCE(vinst->windows[id] != NULL);
	vinst->windows[id] = window;

	mutex_unlock(&vinst->mutex);
}

/*
 * Clear this window from the table(s) of windows for this VAS instance.
 * See also function header of set_vinst_win().
 */
static void clear_vinst_win(struct pnv_vas_window *window)
{
	int id = window->vas_win.winid;
	struct vas_instance *vinst = window->vinst;

	mutex_lock(&vinst->mutex);

	if (!window->user_win && !window->tx_win) {
		WARN_ON_ONCE(!vinst->rxwin[window->vas_win.cop]);
		vinst->rxwin[window->vas_win.cop] = NULL;
	}

	WARN_ON_ONCE(vinst->windows[id] != window);
	vinst->windows[id] = NULL;

	mutex_unlock(&vinst->mutex);
}

static void init_winctx_for_rxwin(struct pnv_vas_window *rxwin,
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
	winctx->wcreds_max = rxwin->vas_win.wcreds_max;
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
		winctx->rx_fifo = 0;
	}

	winctx->lnotify_lpid = rxattr->lnotify_lpid;
	winctx->lnotify_pid = rxattr->lnotify_pid;
	winctx->lnotify_tid = rxattr->lnotify_tid;
	winctx->pswid = rxattr->pswid;
	winctx->dma_type = VAS_DMA_TYPE_INJECT;
	winctx->tc_mode = rxattr->tc_mode;

	winctx->min_scope = VAS_SCOPE_LOCAL;
	winctx->max_scope = VAS_SCOPE_VECTORED_GROUP;
	if (rxwin->vinst->virq)
		winctx->irq_port = rxwin->vinst->irq_port;
}

static bool rx_win_args_valid(enum vas_cop_type cop,
			struct vas_rx_win_attr *attr)
{
	pr_debug("Rxattr: fault %d, notify %d, intr %d, early %d, fifo %d\n",
			attr->fault_win, attr->notify_disable,
			attr->intr_disable, attr->notify_early,
			attr->rx_fifo_size);

	if (cop >= VAS_COP_TYPE_MAX)
		return false;

	if (cop != VAS_COP_TYPE_FTW &&
				attr->rx_fifo_size < VAS_RX_FIFO_SIZE_MIN)
		return false;

	if (attr->rx_fifo_size > VAS_RX_FIFO_SIZE_MAX)
		return false;

	if (!attr->wcreds_max)
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

	if (cop == VAS_COP_TYPE_842 || cop == VAS_COP_TYPE_842_HIPRI ||
		cop == VAS_COP_TYPE_GZIP || cop == VAS_COP_TYPE_GZIP_HIPRI) {
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
		rxattr->rx_win_ord_mode = true;
		rxattr->rej_no_credit = true;
		rxattr->tc_mode = VAS_THRESH_DISABLED;
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
	struct pnv_vas_window *rxwin;
	struct vas_winctx winctx;
	struct vas_instance *vinst;

	trace_vas_rx_win_open(current, vasid, cop, rxattr);

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
		return (struct vas_window *)rxwin;
	}

	rxwin->tx_win = false;
	rxwin->nx_win = rxattr->nx_win;
	rxwin->user_win = rxattr->user_win;
	rxwin->vas_win.cop = cop;
	rxwin->vas_win.wcreds_max = rxattr->wcreds_max;

	init_winctx_for_rxwin(rxwin, rxattr, &winctx);
	init_winctx_regs(rxwin, &winctx);

	set_vinst_win(vinst, rxwin);

	return &rxwin->vas_win;
}
EXPORT_SYMBOL_GPL(vas_rx_win_open);

void vas_init_tx_win_attr(struct vas_tx_win_attr *txattr, enum vas_cop_type cop)
{
	memset(txattr, 0, sizeof(*txattr));

	if (cop == VAS_COP_TYPE_842 || cop == VAS_COP_TYPE_842_HIPRI ||
		cop == VAS_COP_TYPE_GZIP || cop == VAS_COP_TYPE_GZIP_HIPRI) {
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

static void init_winctx_for_txwin(struct pnv_vas_window *txwin,
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

	winctx->wcreds_max = txwin->vas_win.wcreds_max;

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

	winctx->intr_disable = true;
	if (winctx->nx_win)
		winctx->data_stamp = true;

	winctx->lpid = txattr->lpid;
	winctx->pidr = txattr->pidr;
	winctx->rx_win_id = txwin->rxwin->vas_win.winid;
	/*
	 * IRQ and fault window setup is successful. Set fault window
	 * for the send window so that ready to handle faults.
	 */
	if (txwin->vinst->virq)
		winctx->fault_win_id = txwin->vinst->fault_win->vas_win.winid;

	winctx->dma_type = VAS_DMA_TYPE_INJECT;
	winctx->tc_mode = txattr->tc_mode;
	winctx->min_scope = VAS_SCOPE_LOCAL;
	winctx->max_scope = VAS_SCOPE_VECTORED_GROUP;
	if (txwin->vinst->virq)
		winctx->irq_port = txwin->vinst->irq_port;

	winctx->pswid = txattr->pswid ? txattr->pswid :
			encode_pswid(txwin->vinst->vas_id,
			txwin->vas_win.winid);
}

static bool tx_win_args_valid(enum vas_cop_type cop,
			struct vas_tx_win_attr *attr)
{
	if (attr->tc_mode != VAS_THRESH_DISABLED)
		return false;

	if (cop > VAS_COP_TYPE_MAX)
		return false;

	if (attr->wcreds_max > VAS_TX_WCREDS_MAX)
		return false;

	if (attr->user_win) {
		if (attr->rsvd_txbuf_count)
			return false;

		if (cop != VAS_COP_TYPE_FTW && cop != VAS_COP_TYPE_GZIP &&
			cop != VAS_COP_TYPE_GZIP_HIPRI)
			return false;
	}

	return true;
}

struct vas_window *vas_tx_win_open(int vasid, enum vas_cop_type cop,
			struct vas_tx_win_attr *attr)
{
	int rc;
	struct pnv_vas_window *txwin;
	struct pnv_vas_window *rxwin;
	struct vas_winctx winctx;
	struct vas_instance *vinst;

	trace_vas_tx_win_open(current, vasid, cop, attr);

	if (!tx_win_args_valid(cop, attr))
		return ERR_PTR(-EINVAL);

	/*
	 * If caller did not specify a vasid but specified the PSWID of a
	 * receive window (applicable only to FTW windows), use the vasid
	 * from that receive window.
	 */
	if (vasid == -1 && attr->pswid)
		decode_pswid(attr->pswid, &vasid, NULL);

	vinst = find_vas_instance(vasid);
	if (!vinst) {
		pr_devel("vasid %d not found!\n", vasid);
		return ERR_PTR(-EINVAL);
	}

	rxwin = get_vinst_rxwin(vinst, cop, attr->pswid);
	if (IS_ERR(rxwin)) {
		pr_devel("No RxWin for vasid %d, cop %d\n", vasid, cop);
		return (struct vas_window *)rxwin;
	}

	txwin = vas_window_alloc(vinst);
	if (IS_ERR(txwin)) {
		rc = PTR_ERR(txwin);
		goto put_rxwin;
	}

	txwin->vas_win.cop = cop;
	txwin->tx_win = 1;
	txwin->rxwin = rxwin;
	txwin->nx_win = txwin->rxwin->nx_win;
	txwin->user_win = attr->user_win;
	txwin->vas_win.wcreds_max = attr->wcreds_max ?: VAS_WCREDS_DEFAULT;

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
	} else {
		/*
		 * Interrupt hanlder or fault window setup failed. Means
		 * NX can not generate fault for page fault. So not
		 * opening for user space tx window.
		 */
		if (!vinst->virq) {
			rc = -ENODEV;
			goto free_window;
		}
		rc = get_vas_user_win_ref(&txwin->vas_win.task_ref);
		if (rc)
			goto free_window;

		vas_user_win_add_mm_context(&txwin->vas_win.task_ref);
	}

	set_vinst_win(vinst, txwin);

	return &txwin->vas_win;

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
int vas_paste_crb(struct vas_window *vwin, int offset, bool re)
{
	struct pnv_vas_window *txwin;
	int rc;
	void *addr;
	uint64_t val;

	txwin = container_of(vwin, struct pnv_vas_window, vas_win);
	trace_vas_paste_crb(current, txwin);

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

	pr_debug("Txwin #%d: Msg count %llu\n", txwin->vas_win.winid,
			read_hvwc_reg(txwin, VREG(LRFIFO_PUSH)));

	return rc;
}
EXPORT_SYMBOL_GPL(vas_paste_crb);

/*
 * If credit checking is enabled for this window, poll for the return
 * of window credits (i.e for NX engines to process any outstanding CRBs).
 * Since NX-842 waits for the CRBs to be processed before closing the
 * window, we should not have to wait for too long.
 *
 * TODO: We retry in 10ms intervals now. We could/should probably peek at
 *	the VAS_LRFIFO_PUSH_OFFSET register to get an estimate of pending
 *	CRBs on the FIFO and compute the delay dynamically on each retry.
 *	But that is not really needed until we support NX-GZIP access from
 *	user space. (NX-842 driver waits for CSB and Fast thread-wakeup
 *	doesn't use credit checking).
 */
static void poll_window_credits(struct pnv_vas_window *window)
{
	u64 val;
	int creds, mode;
	int count = 0;

	val = read_hvwc_reg(window, VREG(WINCTL));
	if (window->tx_win)
		mode = GET_FIELD(VAS_WINCTL_TX_WCRED_MODE, val);
	else
		mode = GET_FIELD(VAS_WINCTL_RX_WCRED_MODE, val);

	if (!mode)
		return;
retry:
	if (window->tx_win) {
		val = read_hvwc_reg(window, VREG(TX_WCRED));
		creds = GET_FIELD(VAS_TX_WCRED, val);
	} else {
		val = read_hvwc_reg(window, VREG(LRX_WCRED));
		creds = GET_FIELD(VAS_LRX_WCRED, val);
	}

	/*
	 * Takes around few milliseconds to complete all pending requests
	 * and return credits.
	 * TODO: Scan fault FIFO and invalidate CRBs points to this window
	 *       and issue CRB Kill to stop all pending requests. Need only
	 *       if there is a bug in NX or fault handling in kernel.
	 */
	if (creds < window->vas_win.wcreds_max) {
		val = 0;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(10));
		count++;
		/*
		 * Process can not close send window until all credits are
		 * returned.
		 */
		if (!(count % 1000))
			pr_warn_ratelimited("VAS: pid %d stuck. Waiting for credits returned for Window(%d). creds %d, Retries %d\n",
				vas_window_pid(&window->vas_win),
				window->vas_win.winid,
				creds, count);

		goto retry;
	}
}

/*
 * Wait for the window to go to "not-busy" state. It should only take a
 * short time to queue a CRB, so window should not be busy for too long.
 * Trying 5ms intervals.
 */
static void poll_window_busy_state(struct pnv_vas_window *window)
{
	int busy;
	u64 val;
	int count = 0;

retry:
	val = read_hvwc_reg(window, VREG(WIN_STATUS));
	busy = GET_FIELD(VAS_WIN_BUSY, val);
	if (busy) {
		val = 0;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(10));
		count++;
		/*
		 * Takes around few milliseconds to process all pending
		 * requests.
		 */
		if (!(count % 1000))
			pr_warn_ratelimited("VAS: pid %d stuck. Window (ID=%d) is in busy state. Retries %d\n",
				vas_window_pid(&window->vas_win),
				window->vas_win.winid, count);

		goto retry;
	}
}

/*
 * Have the hardware cast a window out of cache and wait for it to
 * be completed.
 *
 * NOTE: It can take a relatively long time to cast the window context
 *	out of the cache. It is not strictly necessary to cast out if:
 *
 *	- we clear the "Pin Window" bit (so hardware is free to evict)
 *
 *	- we re-initialize the window context when it is reassigned.
 *
 *	We do the former in vas_win_close() and latter in vas_win_open().
 *	So, ignoring the cast-out for now. We can add it as needed. If
 *	casting out becomes necessary we should consider offloading the
 *	job to a worker thread, so the window close can proceed quickly.
 */
static void poll_window_castout(struct pnv_vas_window *window)
{
	/* stub for now */
}

/*
 * Unpin and close a window so no new requests are accepted and the
 * hardware can evict this window from cache if necessary.
 */
static void unpin_close_window(struct pnv_vas_window *window)
{
	u64 val;

	val = read_hvwc_reg(window, VREG(WINCTL));
	val = SET_FIELD(VAS_WINCTL_PIN, val, 0);
	val = SET_FIELD(VAS_WINCTL_OPEN, val, 0);
	write_hvwc_reg(window, VREG(WINCTL), val);
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
int vas_win_close(struct vas_window *vwin)
{
	struct pnv_vas_window *window;

	if (!vwin)
		return 0;

	window = container_of(vwin, struct pnv_vas_window, vas_win);

	if (!window->tx_win && atomic_read(&window->num_txwins) != 0) {
		pr_devel("Attempting to close an active Rx window!\n");
		WARN_ON_ONCE(1);
		return -EBUSY;
	}

	unmap_paste_region(window);

	poll_window_busy_state(window);

	unpin_close_window(window);

	poll_window_credits(window);

	clear_vinst_win(window);

	poll_window_castout(window);

	/* if send window, drop reference to matching receive window */
	if (window->tx_win) {
		if (window->user_win) {
			put_vas_user_win_ref(&vwin->task_ref);
			mm_context_remove_vas_window(vwin->task_ref.mm);
		}
		put_rx_win(window->rxwin);
	}

	vas_window_free(window);

	return 0;
}
EXPORT_SYMBOL_GPL(vas_win_close);

/*
 * Return credit for the given window.
 * Send windows and fault window uses credit mechanism as follows:
 *
 * Send windows:
 * - The default number of credits available for each send window is
 *   1024. It means 1024 requests can be issued asynchronously at the
 *   same time. If the credit is not available, that request will be
 *   returned with RMA_Busy.
 * - One credit is taken when NX request is issued.
 * - This credit is returned after NX processed that request.
 * - If NX encounters translation error, kernel will return the
 *   credit on the specific send window after processing the fault CRB.
 *
 * Fault window:
 * - The total number credits available is FIFO_SIZE/CRB_SIZE.
 *   Means 4MB/128 in the current implementation. If credit is not
 *   available, RMA_Reject is returned.
 * - A credit is taken when NX pastes CRB in fault FIFO.
 * - The kernel with return credit on fault window after reading entry
 *   from fault FIFO.
 */
void vas_return_credit(struct pnv_vas_window *window, bool tx)
{
	uint64_t val;

	val = 0ULL;
	if (tx) { /* send window */
		val = SET_FIELD(VAS_TX_WCRED, val, 1);
		write_hvwc_reg(window, VREG(TX_WCRED_ADDER), val);
	} else {
		val = SET_FIELD(VAS_LRX_WCRED, val, 1);
		write_hvwc_reg(window, VREG(LRX_WCRED_ADDER), val);
	}
}

struct pnv_vas_window *vas_pswid_to_window(struct vas_instance *vinst,
		uint32_t pswid)
{
	struct pnv_vas_window *window;
	int winid;

	if (!pswid) {
		pr_devel("%s: called for pswid 0!\n", __func__);
		return ERR_PTR(-ESRCH);
	}

	decode_pswid(pswid, NULL, &winid);

	if (winid >= VAS_WINDOWS_PER_CHIP)
		return ERR_PTR(-ESRCH);

	/*
	 * If application closes the window before the hardware
	 * returns the fault CRB, we should wait in vas_win_close()
	 * for the pending requests. so the window must be active
	 * and the process alive.
	 *
	 * If its a kernel process, we should not get any faults and
	 * should not get here.
	 */
	window = vinst->windows[winid];

	if (!window) {
		pr_err("PSWID decode: Could not find window for winid %d pswid %d vinst 0x%p\n",
			winid, pswid, vinst);
		return NULL;
	}

	/*
	 * Do some sanity checks on the decoded window.  Window should be
	 * NX GZIP user send window. FTW windows should not incur faults
	 * since their CRBs are ignored (not queued on FIFO or processed
	 * by NX).
	 */
	if (!window->tx_win || !window->user_win || !window->nx_win ||
			window->vas_win.cop == VAS_COP_TYPE_FAULT ||
			window->vas_win.cop == VAS_COP_TYPE_FTW) {
		pr_err("PSWID decode: id %d, tx %d, user %d, nx %d, cop %d\n",
			winid, window->tx_win, window->user_win,
			window->nx_win, window->vas_win.cop);
		WARN_ON(1);
	}

	return window;
}

static struct vas_window *vas_user_win_open(int vas_id, u64 flags,
				enum vas_cop_type cop_type)
{
	struct vas_tx_win_attr txattr = {};

	vas_init_tx_win_attr(&txattr, cop_type);

	txattr.lpid = mfspr(SPRN_LPID);
	txattr.pidr = mfspr(SPRN_PID);
	txattr.user_win = true;
	txattr.rsvd_txbuf_count = false;
	txattr.pswid = false;

	pr_devel("Pid %d: Opening txwin, PIDR %ld\n", txattr.pidr,
				mfspr(SPRN_PID));

	return vas_tx_win_open(vas_id, cop_type, &txattr);
}

static u64 vas_user_win_paste_addr(struct vas_window *txwin)
{
	struct pnv_vas_window *win;
	u64 paste_addr;

	win = container_of(txwin, struct pnv_vas_window, vas_win);
	vas_win_paste_addr(win, &paste_addr, NULL);

	return paste_addr;
}

static int vas_user_win_close(struct vas_window *txwin)
{
	vas_win_close(txwin);

	return 0;
}

static const struct vas_user_win_ops vops =  {
	.open_win	=	vas_user_win_open,
	.paste_addr	=	vas_user_win_paste_addr,
	.close_win	=	vas_user_win_close,
};

/*
 * Supporting only nx-gzip coprocessor type now, but this API code
 * extended to other coprocessor types later.
 */
int vas_register_api_powernv(struct module *mod, enum vas_cop_type cop_type,
			     const char *name)
{

	return vas_register_coproc_api(mod, cop_type, name, &vops);
}
EXPORT_SYMBOL_GPL(vas_register_api_powernv);

void vas_unregister_api_powernv(void)
{
	vas_unregister_coproc_api();
}
EXPORT_SYMBOL_GPL(vas_unregister_api_powernv);
