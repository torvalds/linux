/*
 * DHD Bus Module for PCIE
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhd_pcie.c 702835 2017-06-05 07:19:55Z $
 */

/* include files */
#include <typedefs.h>
#include <bcmutils.h>
#include <bcmdevs.h>
#include <siutils.h>
#include <sbpcmcia.h>
#include <hndoobr.h>
#include <hndsoc.h>
#include <hndpmu.h>
#include <etd.h>
#include <hnd_debug.h>
#include <sbchipc.h>
#include <sbhndarm.h>
#include <hnd_armtrap.h>
#if defined(DHD_DEBUG)
#include <hnd_cons.h>
#endif /* defined(DHD_DEBUG) */
#include <dngl_stats.h>
#include <pcie_core.h>
#include <dhd.h>
#include <dhd_bus.h>
#include <dhd_flowring.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <dhd_debug.h>
#include <dhd_daemon.h>
#include <dhdioctl.h>
#include <sdiovar.h>
#include <bcmmsgbuf.h>
#include <pcicfg.h>
#include <dhd_pcie.h>
#include <bcmpcie.h>
#include <bcmendian.h>
#include <bcmstdlib_s.h>
#ifdef DHDTCPACK_SUPPRESS
#include <dhd_ip.h>
#endif /* DHDTCPACK_SUPPRESS */
#include <bcmevent.h>
#include <trxhdr.h>

extern uint32 hw_module_variant;
#include <pcie_core.h>

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
#include <linux/pm_runtime.h>
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

#if defined(DEBUGGER) || defined(DHD_DSCOPE)
#include <debugger.h>
#endif /* DEBUGGER || DHD_DSCOPE */

#ifdef DNGL_AXI_ERROR_LOGGING
#include <dhd_linux_wq.h>
#include <dhd_linux.h>
#endif /* DNGL_AXI_ERROR_LOGGING */

#if defined(DHD_CONTROL_PCIE_CPUCORE_WIFI_TURNON)
#include <dhd_linux_priv.h>
#endif /* DHD_CONTROL_PCIE_CPUCORE_WIFI_TURNON */

#include <otpdefs.h>
#define EXTENDED_PCIE_DEBUG_DUMP 1	/* Enable Extended pcie registers dump */

#define MEMBLOCK	2048		/* Block size used for downloading of dongle image */
#define MAX_WKLK_IDLE_CHECK	3	/* times wake_lock checked before deciding not to suspend */

#define	DHD_MAX_ITEMS_HPP_TXCPL_RING	512
#define	DHD_MAX_ITEMS_HPP_RXCPL_RING	512

#define ARMCR4REG_CORECAP	(0x4/sizeof(uint32))
#define ARMCR4REG_MPUCTRL	(0x90/sizeof(uint32))
#define ACC_MPU_SHIFT		25
#define ACC_MPU_MASK		(0x1u << ACC_MPU_SHIFT)

#define ARMCR4REG_BANKIDX	(0x40/sizeof(uint32))
#define ARMCR4REG_BANKPDA	(0x4C/sizeof(uint32))
/* Temporary war to fix precommit till sync issue between trunk & precommit branch is resolved */

/* CTO Prevention Recovery */
#ifdef BCMQT_HW
#define CTO_TO_CLEAR_WAIT_MS 10000
#define CTO_TO_CLEAR_WAIT_MAX_CNT 100
#else
#define CTO_TO_CLEAR_WAIT_MS 1000
#define CTO_TO_CLEAR_WAIT_MAX_CNT 10
#endif // endif

/* Fetch address of a member in the pciedev_shared structure in dongle memory */
#define DHD_PCIE_SHARED_MEMBER_ADDR(bus, member) \
	(bus)->shared_addr + OFFSETOF(pciedev_shared_t, member)

/* Fetch address of a member in rings_info_ptr structure in dongle memory */
#define DHD_RING_INFO_MEMBER_ADDR(bus, member) \
	(bus)->pcie_sh->rings_info_ptr + OFFSETOF(ring_info_t, member)

/* Fetch address of a member in the ring_mem structure in dongle memory */
#define DHD_RING_MEM_MEMBER_ADDR(bus, ringid, member) \
	(bus)->ring_sh[ringid].ring_mem_addr + OFFSETOF(ring_mem_t, member)

#if defined(SUPPORT_MULTIPLE_BOARD_REV)
	extern unsigned int system_rev;
#endif /* SUPPORT_MULTIPLE_BOARD_REV */

/* DHD module parameter */
extern uint32 hw_module_variant;

#ifdef EWP_EDL
extern int host_edl_support;
#endif // endif

#define D2H_HS_START_STATE (1 << D2H_START_SHIFT)
#define D2H_HS_READY_STATE (1 << D2H_START_SHIFT | 1 << D2H_READY_SHIFT)

/* This can be overwritten by module parameter(dma_ring_indices) defined in dhd_linux.c */
uint dma_ring_indices = 0;
/* This can be overwritten by module parameter(h2d_phase) defined in dhd_linux.c */
bool h2d_phase = 0;
/* This can be overwritten by module parameter(force_trap_bad_h2d_phase)
 * defined in dhd_linux.c
 */
bool force_trap_bad_h2d_phase = 0;

int dhd_dongle_memsize;
int dhd_dongle_ramsize;
struct dhd_bus *g_dhd_bus = NULL;
#ifdef DNGL_AXI_ERROR_LOGGING
static void dhd_log_dump_axi_error(uint8 *axi_err);
#endif /* DNGL_AXI_ERROR_LOGGING */

static int dhdpcie_checkdied(dhd_bus_t *bus, char *data, uint size);
static int dhdpcie_bus_readconsole(dhd_bus_t *bus);
#if defined(DHD_FW_COREDUMP)
static int dhdpcie_mem_dump(dhd_bus_t *bus);
static int dhdpcie_get_mem_dump(dhd_bus_t *bus);
#endif /* DHD_FW_COREDUMP */

static int dhdpcie_bus_membytes(dhd_bus_t *bus, bool write, ulong address, uint8 *data, uint size);
static int dhdpcie_bus_doiovar(dhd_bus_t *bus, const bcm_iovar_t *vi, uint32 actionid,
	const char *name, void *params,
	int plen, void *arg, int len, int val_size);
static int dhdpcie_bus_lpback_req(struct  dhd_bus *bus, uint32 intval);
static int dhdpcie_bus_dmaxfer_req(struct  dhd_bus *bus,
	uint32 len, uint32 srcdelay, uint32 destdelay,
	uint32 d11_lpbk, uint32 core_num, uint32 wait);
static int dhdpcie_bus_download_state(dhd_bus_t *bus, bool enter);
static int dhdpcie_handshake_msg_reg_write(si_t *sih, osl_t *osh, volatile void *addr,
	uint *buffer);
static int dhdpcie_handshake_msg_reg_read(si_t *sih, osl_t *osh, volatile void *addr,
	uint *buffer);
static int dhdpcie_dongle_host_handshake_spinwait(si_t *sih, osl_t *osh, volatile void *addr,
	uint32 bitshift, uint32 us);
static int dhdpcie_dongle_host_get_handshake_address(si_t *sih, osl_t *osh, hs_addrs_t *addr);
static int dhdpcie_dongle_host_pre_handshake(si_t *sih, osl_t *osh, hs_addrs_t *addr);
static int dhdpcie_dongle_host_post_handshake(si_t *sih, osl_t *osh, hs_addrs_t *addr);
static int dhdpcie_dongle_host_chk_validation(si_t *sih, osl_t *osh, hs_addrs_t *addr);
int dhdpcie_dongle_host_pre_wd_reset_sequence(si_t *sih, osl_t *osh);
int dhdpcie_dongle_host_post_wd_reset_sequence(si_t *sih, osl_t *osh);
int dhdpcie_dongle_host_pre_chipid_access_sequence(osl_t *osh, volatile void *regva);
static int dhdpcie_dongle_host_post_varswrite(dhd_bus_t *bus, hs_addrs_t *addr);
static int _dhdpcie_download_firmware(struct dhd_bus *bus);
static int dhdpcie_download_firmware(dhd_bus_t *bus, osl_t *osh);
static int dhdpcie_bus_write_vars(dhd_bus_t *bus);
static bool dhdpcie_bus_process_mailbox_intr(dhd_bus_t *bus, uint32 intstatus);
static bool dhdpci_bus_read_frames(dhd_bus_t *bus);
static int dhdpcie_readshared_console(dhd_bus_t *bus);
static int dhdpcie_readshared(dhd_bus_t *bus);
static void dhdpcie_init_shared_addr(dhd_bus_t *bus);
static bool dhdpcie_dongle_attach(dhd_bus_t *bus);
static void dhdpcie_bus_dongle_setmemsize(dhd_bus_t *bus, int mem_size);
static void dhdpcie_bus_release_dongle(dhd_bus_t *bus, osl_t *osh,
	bool dongle_isolation, bool reset_flag);
static void dhdpcie_bus_release_malloc(dhd_bus_t *bus, osl_t *osh);
static int dhdpcie_downloadvars(dhd_bus_t *bus, void *arg, int len);
static void dhdpcie_setbar1win(dhd_bus_t *bus, uint32 addr);
static uint8 dhdpcie_bus_rtcm8(dhd_bus_t *bus, ulong offset);
static void dhdpcie_bus_wtcm8(dhd_bus_t *bus, ulong offset, uint8 data);
static void dhdpcie_bus_wtcm16(dhd_bus_t *bus, ulong offset, uint16 data);
static uint16 dhdpcie_bus_rtcm16(dhd_bus_t *bus, ulong offset);
static void dhdpcie_bus_wtcm32(dhd_bus_t *bus, ulong offset, uint32 data);
static uint32 dhdpcie_bus_rtcm32(dhd_bus_t *bus, ulong offset);
#ifdef DHD_SUPPORT_64BIT
static void dhdpcie_bus_wtcm64(dhd_bus_t *bus, ulong offset, uint64 data) __attribute__ ((used));
static uint64 dhdpcie_bus_rtcm64(dhd_bus_t *bus, ulong offset) __attribute__ ((used));
#endif /* DHD_SUPPORT_64BIT */
static void dhdpcie_bus_cfg_set_bar0_win(dhd_bus_t *bus, uint32 data);
static void dhdpcie_bus_reg_unmap(osl_t *osh, volatile char *addr, int size);
static int dhdpcie_cc_nvmshadow(dhd_bus_t *bus, struct bcmstrbuf *b);
static int dhdpcie_sromotp_customvar(dhd_bus_t *bus, uint32 *customvar1, uint32 *customvar2);
static void dhdpcie_fw_trap(dhd_bus_t *bus);
static void dhd_fillup_ring_sharedptr_info(dhd_bus_t *bus, ring_info_t *ring_info);
static void dhdpcie_handle_mb_data(dhd_bus_t *bus);
extern void dhd_dpc_enable(dhd_pub_t *dhdp);
extern void dhd_dpc_kill(dhd_pub_t *dhdp);

#ifdef IDLE_TX_FLOW_MGMT
static void dhd_bus_check_idle_scan(dhd_bus_t *bus);
static void dhd_bus_idle_scan(dhd_bus_t *bus);
#endif /* IDLE_TX_FLOW_MGMT */

#ifdef EXYNOS_PCIE_DEBUG
extern void exynos_pcie_register_dump(int ch_num);
#endif /* EXYNOS_PCIE_DEBUG */

#if defined(DHD_H2D_LOG_TIME_SYNC)
static void dhdpci_bus_rte_log_time_sync_poll(dhd_bus_t *bus);
#endif /* DHD_H2D_LOG_TIME_SYNC */

#define     PCI_VENDOR_ID_BROADCOM          0x14e4
#define     PCI_VENDOR_ID_CYPRESS           0x12be

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
#define MAX_D3_ACK_TIMEOUT	100
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

#define DHD_DEFAULT_DOORBELL_TIMEOUT 200	/* ms */
static bool dhdpcie_check_firmware_compatible(uint32 f_api_version, uint32 h_api_version);
static int dhdpcie_cto_error_recovery(struct dhd_bus *bus);

static int dhdpcie_init_d11status(struct dhd_bus *bus);

static int dhdpcie_wrt_rnd(struct dhd_bus *bus);

extern uint16 dhd_prot_get_h2d_max_txpost(dhd_pub_t *dhd);
extern void dhd_prot_set_h2d_max_txpost(dhd_pub_t *dhd, uint16 max_txpost);

#ifdef DHD_HP2P
extern enum hrtimer_restart dhd_hp2p_write(struct hrtimer *timer);
static uint16 dhd_bus_set_hp2p_ring_max_size(struct dhd_bus *bus, bool tx, uint16 val);
#endif // endif
#define NUM_PATTERNS 2
static bool dhd_bus_tcm_test(struct dhd_bus *bus);

/* IOVar table */
enum {
	IOV_INTR = 1,
#ifdef DHD_BUS_MEM_ACCESS
	IOV_MEMBYTES,
#endif /* DHD_BUS_MEM_ACCESS */
	IOV_MEMSIZE,
	IOV_SET_DOWNLOAD_STATE,
	IOV_DEVRESET,
	IOV_VARS,
	IOV_MSI_SIM,
	IOV_PCIE_LPBK,
	IOV_CC_NVMSHADOW,
	IOV_RAMSIZE,
	IOV_RAMSTART,
	IOV_SLEEP_ALLOWED,
	IOV_PCIE_DMAXFER,
	IOV_PCIE_SUSPEND,
#ifdef DHD_PCIE_REG_ACCESS
	IOV_PCIEREG,
	IOV_PCIECFGREG,
	IOV_PCIECOREREG,
	IOV_PCIESERDESREG,
	IOV_PCIEASPM,
	IOV_BAR0_SECWIN_REG,
	IOV_SBREG,
#endif /* DHD_PCIE_REG_ACCESS */
	IOV_DONGLEISOLATION,
	IOV_LTRSLEEPON_UNLOOAD,
	IOV_METADATA_DBG,
	IOV_RX_METADATALEN,
	IOV_TX_METADATALEN,
	IOV_TXP_THRESHOLD,
	IOV_BUZZZ_DUMP,
	IOV_DUMP_RINGUPD_BLOCK,
	IOV_DMA_RINGINDICES,
	IOV_FORCE_FW_TRAP,
	IOV_DB1_FOR_MB,
	IOV_FLOW_PRIO_MAP,
#ifdef DHD_PCIE_RUNTIMEPM
	IOV_IDLETIME,
#endif /* DHD_PCIE_RUNTIMEPM */
	IOV_RXBOUND,
	IOV_TXBOUND,
	IOV_HANGREPORT,
	IOV_H2D_MAILBOXDATA,
	IOV_INFORINGS,
	IOV_H2D_PHASE,
	IOV_H2D_ENABLE_TRAP_BADPHASE,
	IOV_H2D_TXPOST_MAX_ITEM,
	IOV_TRAPDATA,
	IOV_TRAPDATA_RAW,
	IOV_CTO_PREVENTION,
	IOV_PCIE_WD_RESET,
	IOV_DUMP_DONGLE,
	IOV_HWA_ENAB_BMAP,
	IOV_IDMA_ENABLE,
	IOV_IFRM_ENABLE,
	IOV_CLEAR_RING,
	IOV_DAR_ENABLE,
	IOV_DNGL_CAPS,   /**< returns string with dongle capabilities */
#if defined(DEBUGGER) || defined(DHD_DSCOPE)
	IOV_GDB_SERVER,  /**< starts gdb server on given interface */
#endif /* DEBUGGER || DHD_DSCOPE */
	IOV_INB_DW_ENABLE,
	IOV_CTO_THRESHOLD,
	IOV_HSCBSIZE, /* get HSCB buffer size */
#ifdef DHD_BUS_MEM_ACCESS
	IOV_HSCBBYTES, /* copy HSCB buffer */
#endif // endif
	IOV_HP2P_ENABLE,
	IOV_HP2P_PKT_THRESHOLD,
	IOV_HP2P_TIME_THRESHOLD,
	IOV_HP2P_PKT_EXPIRY,
	IOV_HP2P_TXCPL_MAXITEMS,
	IOV_HP2P_RXCPL_MAXITEMS,
	IOV_EXTDTXS_IN_TXCPL,
	IOV_HOSTRDY_AFTER_INIT,
	IOV_PCIE_LAST /**< unused IOVAR */
};

const bcm_iovar_t dhdpcie_iovars[] = {
	{"intr",	IOV_INTR,	0, 	0, IOVT_BOOL,	0 },
#ifdef DHD_BUS_MEM_ACCESS
	{"membytes",	IOV_MEMBYTES,	0, 	0, IOVT_BUFFER,	2 * sizeof(int) },
#endif /* DHD_BUS_MEM_ACCESS */
	{"memsize",	IOV_MEMSIZE,	0, 	0, IOVT_UINT32,	0 },
	{"dwnldstate",	IOV_SET_DOWNLOAD_STATE,	0, 	0, IOVT_BOOL,	0 },
	{"vars",	IOV_VARS,	0, 	0, IOVT_BUFFER,	0 },
	{"devreset",	IOV_DEVRESET,	0, 	0, IOVT_UINT8,	0 },
	{"pcie_device_trap", IOV_FORCE_FW_TRAP, 0, 	0, 0,	0 },
	{"pcie_lpbk",	IOV_PCIE_LPBK,	0,	0, IOVT_UINT32,	0 },
	{"cc_nvmshadow", IOV_CC_NVMSHADOW, 0,	0, IOVT_BUFFER, 0 },
	{"ramsize",	IOV_RAMSIZE,	0, 	0, IOVT_UINT32,	0 },
	{"ramstart",	IOV_RAMSTART,	0, 	0, IOVT_UINT32,	0 },
#ifdef DHD_PCIE_REG_ACCESS
	{"pciereg",	IOV_PCIEREG,	0, 	0, IOVT_BUFFER,	2 * sizeof(int32) },
	{"pciecfgreg",	IOV_PCIECFGREG,	0, 	0, IOVT_BUFFER,	2 * sizeof(int32) },
	{"pciecorereg",	IOV_PCIECOREREG,	0, 	0, IOVT_BUFFER,	2 * sizeof(int32) },
	{"pcieserdesreg",	IOV_PCIESERDESREG,	0, 	0, IOVT_BUFFER,	3 * sizeof(int32) },
	{"bar0secwinreg",	IOV_BAR0_SECWIN_REG,	0, 	0, IOVT_BUFFER,	sizeof(sdreg_t) },
	{"sbreg",	IOV_SBREG,	0,	0, IOVT_BUFFER,	sizeof(uint8) },
#endif /* DHD_PCIE_REG_ACCESS */
	{"pcie_dmaxfer", IOV_PCIE_DMAXFER, 0, 0, IOVT_BUFFER, sizeof(dma_xfer_info_t)},
	{"pcie_suspend", IOV_PCIE_SUSPEND,	DHD_IOVF_PWRREQ_BYPASS,	0, IOVT_UINT32,	0 },
	{"sleep_allowed",	IOV_SLEEP_ALLOWED,	0,	0, IOVT_BOOL,	0 },
	{"dngl_isolation", IOV_DONGLEISOLATION,	0, 	0, IOVT_UINT32,	0 },
	{"ltrsleep_on_unload", IOV_LTRSLEEPON_UNLOOAD,	0,	0, IOVT_UINT32,	0 },
	{"dump_ringupdblk", IOV_DUMP_RINGUPD_BLOCK,	0, 	0, IOVT_BUFFER,	0 },
	{"dma_ring_indices", IOV_DMA_RINGINDICES,	0, 	0, IOVT_UINT32,	0},
	{"metadata_dbg", IOV_METADATA_DBG,	0,	0, IOVT_BOOL,	0 },
	{"rx_metadata_len", IOV_RX_METADATALEN,	0, 	0, IOVT_UINT32,	0 },
	{"tx_metadata_len", IOV_TX_METADATALEN,	0, 	0, IOVT_UINT32,	0 },
	{"db1_for_mb", IOV_DB1_FOR_MB,	0, 	0, IOVT_UINT32,	0 },
	{"txp_thresh", IOV_TXP_THRESHOLD,	0,	0, IOVT_UINT32,	0 },
	{"buzzz_dump", IOV_BUZZZ_DUMP,		0, 	0, IOVT_UINT32,	0 },
	{"flow_prio_map", IOV_FLOW_PRIO_MAP,	0, 	0, IOVT_UINT32,	0 },
#ifdef DHD_PCIE_RUNTIMEPM
	{"idletime",    IOV_IDLETIME,   0,	0, IOVT_INT32,     0 },
#endif /* DHD_PCIE_RUNTIMEPM */
	{"rxbound",     IOV_RXBOUND,    0, 0,	IOVT_UINT32,    0 },
	{"txbound",     IOV_TXBOUND,    0, 0,	IOVT_UINT32,    0 },
#ifdef DHD_PCIE_REG_ACCESS
	{"aspm", IOV_PCIEASPM, 0, 0, IOVT_INT32, 0 },
#endif /* DHD_PCIE_REG_ACCESS */
	{"fw_hang_report", IOV_HANGREPORT,	0, 0,	IOVT_BOOL,	0 },
	{"h2d_mb_data",     IOV_H2D_MAILBOXDATA,    0, 0,      IOVT_UINT32,    0 },
	{"inforings",   IOV_INFORINGS,    0, 0,      IOVT_UINT32,    0 },
	{"h2d_phase",   IOV_H2D_PHASE,    0, 0,      IOVT_UINT32,    0 },
	{"force_trap_bad_h2d_phase", IOV_H2D_ENABLE_TRAP_BADPHASE,    0, 0,
	IOVT_UINT32,    0 },
	{"h2d_max_txpost",   IOV_H2D_TXPOST_MAX_ITEM,    0, 0,      IOVT_UINT32,    0 },
	{"trap_data",	IOV_TRAPDATA,	0, 0,	IOVT_BUFFER,	0 },
	{"trap_data_raw",	IOV_TRAPDATA_RAW,	0, 0,	IOVT_BUFFER,	0 },
	{"cto_prevention",	IOV_CTO_PREVENTION,	0, 0,	IOVT_UINT32,	0 },
	{"pcie_wd_reset",	IOV_PCIE_WD_RESET,	0,	0, IOVT_BOOL,	0 },
	{"dump_dongle", IOV_DUMP_DONGLE, 0, 0, IOVT_BUFFER,
	MAX(sizeof(dump_dongle_in_t), sizeof(dump_dongle_out_t))},
	{"clear_ring",   IOV_CLEAR_RING,    0, 0,  IOVT_UINT32,    0 },
	{"hwa_enab_bmap",   IOV_HWA_ENAB_BMAP,    0, 0,  IOVT_UINT32,    0 },
	{"idma_enable",   IOV_IDMA_ENABLE,    0, 0,  IOVT_UINT32,    0 },
	{"ifrm_enable",   IOV_IFRM_ENABLE,    0, 0,  IOVT_UINT32,    0 },
	{"dar_enable",   IOV_DAR_ENABLE,    0, 0,  IOVT_UINT32,    0 },
	{"cap", IOV_DNGL_CAPS,	0, 0, IOVT_BUFFER,	0},
#if defined(DEBUGGER) || defined(DHD_DSCOPE)
	{"gdb_server", IOV_GDB_SERVER,    0, 0,      IOVT_UINT32,    0 },
#endif /* DEBUGGER || DHD_DSCOPE */
	{"inb_dw_enable",   IOV_INB_DW_ENABLE,    0, 0,  IOVT_UINT32,    0 },
	{"cto_threshold",	IOV_CTO_THRESHOLD,	0,	0, IOVT_UINT32,	0 },
	{"hscbsize",	IOV_HSCBSIZE,	0,	0,	IOVT_UINT32,	0 },
#ifdef DHD_BUS_MEM_ACCESS
	{"hscbbytes",	IOV_HSCBBYTES,	0,	0,	IOVT_BUFFER,	2 * sizeof(int32) },
#endif // endif
#ifdef DHD_HP2P
	{"hp2p_enable", IOV_HP2P_ENABLE,	0,	0, IOVT_UINT32,	0 },
	{"hp2p_pkt_thresh", IOV_HP2P_PKT_THRESHOLD,	0,	0, IOVT_UINT32,	0 },
	{"hp2p_time_thresh", IOV_HP2P_TIME_THRESHOLD,	0,	0, IOVT_UINT32,	0 },
	{"hp2p_pkt_expiry", IOV_HP2P_PKT_EXPIRY,	0,	0, IOVT_UINT32,	0 },
	{"hp2p_txcpl_maxitems", IOV_HP2P_TXCPL_MAXITEMS,	0,	0, IOVT_UINT32,	0 },
	{"hp2p_rxcpl_maxitems", IOV_HP2P_RXCPL_MAXITEMS,	0,	0, IOVT_UINT32,	0 },
#endif // endif
	{"extdtxs_in_txcpl", IOV_EXTDTXS_IN_TXCPL,	0,	0, IOVT_UINT32,	0 },
	{"hostrdy_after_init", IOV_HOSTRDY_AFTER_INIT,	0,	0, IOVT_UINT32,	0 },
	{NULL, 0, 0, 0, 0, 0 }
};

#ifdef BCMQT
#define MAX_READ_TIMEOUT	200 * 1000 * 1000
#else
#define MAX_READ_TIMEOUT	5 * 1000 * 1000
#endif // endif

#ifndef DHD_RXBOUND
#define DHD_RXBOUND		64
#endif // endif
#ifndef DHD_TXBOUND
#define DHD_TXBOUND		64
#endif // endif

#define DHD_INFORING_BOUND	32
#define DHD_BTLOGRING_BOUND	32

uint dhd_rxbound = DHD_RXBOUND;
uint dhd_txbound = DHD_TXBOUND;

#if defined(DEBUGGER) || defined(DHD_DSCOPE)
/** the GDB debugger layer will call back into this (bus) layer to read/write dongle memory */
static struct dhd_gdb_bus_ops_s  bus_ops = {
	.read_u16 = dhdpcie_bus_rtcm16,
	.read_u32 = dhdpcie_bus_rtcm32,
	.write_u32 = dhdpcie_bus_wtcm32,
};
#endif /* DEBUGGER || DHD_DSCOPE */

bool
dhd_bus_get_flr_force_fail(struct dhd_bus *bus)
{
	return bus->flr_force_fail;
}

/**
 * Register/Unregister functions are called by the main DHD entry point (eg module insertion) to
 * link with the bus driver, in order to look for or await the device.
 */
int
dhd_bus_register(void)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	return dhdpcie_bus_register();
}

void
dhd_bus_unregister(void)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	dhdpcie_bus_unregister();
	return;
}

/** returns a host virtual address */
uint32 *
dhdpcie_bus_reg_map(osl_t *osh, ulong addr, int size)
{
	return (uint32 *)REG_MAP(addr, size);
}

void
dhdpcie_bus_reg_unmap(osl_t *osh, volatile char *addr, int size)
{
	REG_UNMAP(addr);
	return;
}

/**
 * retrun H2D Doorbell registers address
 * use DAR registers instead of enum register for corerev >= 23 (4347B0)
 */
static INLINE uint
dhd_bus_db0_addr_get(struct dhd_bus *bus)
{
	uint addr = PCIH2D_MailBox;
	uint dar_addr = DAR_PCIH2D_DB0_0(bus->sih->buscorerev);

	return ((DAR_ACTIVE(bus->dhd)) ? dar_addr : addr);
}

static INLINE uint
dhd_bus_db0_addr_2_get(struct dhd_bus *bus)
{
	return ((DAR_ACTIVE(bus->dhd)) ? DAR_PCIH2D_DB2_0(bus->sih->buscorerev) : PCIH2D_MailBox_2);
}

static INLINE uint
dhd_bus_db1_addr_get(struct dhd_bus *bus)
{
	return ((DAR_ACTIVE(bus->dhd)) ? DAR_PCIH2D_DB0_1(bus->sih->buscorerev) : PCIH2D_DB1);
}

static INLINE uint
dhd_bus_db1_addr_1_get(struct dhd_bus *bus)
{
	return ((DAR_ACTIVE(bus->dhd)) ? DAR_PCIH2D_DB1_1(bus->sih->buscorerev) : PCIH2D_DB1_1);
}

/*
 * WAR for SWWLAN-215055 - [4378B0] ARM fails to boot without DAR WL domain request
 */
static INLINE void
dhd_bus_pcie_pwr_req_wl_domain(struct dhd_bus *bus, bool enable)
{
	if (enable) {
		si_corereg(bus->sih, bus->sih->buscoreidx,
			DAR_PCIE_PWR_CTRL((bus->sih)->buscorerev),
			SRPWR_DMN1_ARMBPSD_MASK << SRPWR_REQON_SHIFT,
			SRPWR_DMN1_ARMBPSD_MASK << SRPWR_REQON_SHIFT);
	} else {
		si_corereg(bus->sih, bus->sih->buscoreidx,
			DAR_PCIE_PWR_CTRL((bus->sih)->buscorerev),
			SRPWR_DMN1_ARMBPSD_MASK << SRPWR_REQON_SHIFT, 0);
	}
}

static INLINE void
_dhd_bus_pcie_pwr_req_clear_cmn(struct dhd_bus *bus)
{
	uint mask;

	/*
	 * If multiple de-asserts, decrement ref and return
	 * Clear power request when only one pending
	 * so initial request is not removed unexpectedly
	 */
	if (bus->pwr_req_ref > 1) {
		bus->pwr_req_ref--;
		return;
	}

	ASSERT(bus->pwr_req_ref == 1);

	if (MULTIBP_ENAB(bus->sih)) {
		/* Common BP controlled by HW so only need to toggle WL/ARM backplane */
		mask = SRPWR_DMN1_ARMBPSD_MASK;
	} else {
		mask = SRPWR_DMN0_PCIE_MASK | SRPWR_DMN1_ARMBPSD_MASK;
	}

	si_srpwr_request(bus->sih, mask, 0);
	bus->pwr_req_ref = 0;
}

static INLINE void
dhd_bus_pcie_pwr_req_clear(struct dhd_bus *bus)
{
	unsigned long flags = 0;

	DHD_GENERAL_LOCK(bus->dhd, flags);
	_dhd_bus_pcie_pwr_req_clear_cmn(bus);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);
}

static INLINE void
dhd_bus_pcie_pwr_req_clear_nolock(struct dhd_bus *bus)
{
	_dhd_bus_pcie_pwr_req_clear_cmn(bus);
}

static INLINE void
_dhd_bus_pcie_pwr_req_cmn(struct dhd_bus *bus)
{
	uint mask, val;

	/* If multiple request entries, increment reference and return */
	if (bus->pwr_req_ref > 0) {
		bus->pwr_req_ref++;
		return;
	}

	ASSERT(bus->pwr_req_ref == 0);

	if (MULTIBP_ENAB(bus->sih)) {
		/* Common BP controlled by HW so only need to toggle WL/ARM backplane */
		mask = SRPWR_DMN1_ARMBPSD_MASK;
		val = SRPWR_DMN1_ARMBPSD_MASK;
	} else {
		mask = SRPWR_DMN0_PCIE_MASK | SRPWR_DMN1_ARMBPSD_MASK;
		val = SRPWR_DMN0_PCIE_MASK | SRPWR_DMN1_ARMBPSD_MASK;
	}

	si_srpwr_request(bus->sih, mask, val);

	bus->pwr_req_ref = 1;
}

static INLINE void
dhd_bus_pcie_pwr_req(struct dhd_bus *bus)
{
	unsigned long flags = 0;

	DHD_GENERAL_LOCK(bus->dhd, flags);
	_dhd_bus_pcie_pwr_req_cmn(bus);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);
}

static INLINE void
_dhd_bus_pcie_pwr_req_pd0123_cmn(struct dhd_bus *bus)
{
	uint mask, val;

	mask = SRPWR_DMN_ALL_MASK(bus->sih);
	val = SRPWR_DMN_ALL_MASK(bus->sih);

	si_srpwr_request(bus->sih, mask, val);
}

static INLINE void
dhd_bus_pcie_pwr_req_reload_war(struct dhd_bus *bus)
{
	unsigned long flags = 0;

	DHD_GENERAL_LOCK(bus->dhd, flags);
	_dhd_bus_pcie_pwr_req_pd0123_cmn(bus);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);
}

static INLINE void
_dhd_bus_pcie_pwr_req_clear_pd0123_cmn(struct dhd_bus *bus)
{
	uint mask;

	mask = SRPWR_DMN_ALL_MASK(bus->sih);

	si_srpwr_request(bus->sih, mask, 0);
}

static INLINE void
dhd_bus_pcie_pwr_req_clear_reload_war(struct dhd_bus *bus)
{
	unsigned long flags = 0;

	DHD_GENERAL_LOCK(bus->dhd, flags);
	_dhd_bus_pcie_pwr_req_clear_pd0123_cmn(bus);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);
}

static INLINE void
dhd_bus_pcie_pwr_req_nolock(struct dhd_bus *bus)
{
	_dhd_bus_pcie_pwr_req_cmn(bus);
}

bool
dhdpcie_chip_support_msi(dhd_bus_t *bus)
{
	DHD_ERROR(("%s: buscorerev=%d chipid=0x%x\n",
		__FUNCTION__, bus->sih->buscorerev, si_chipid(bus->sih)));
	if (bus->sih->buscorerev <= 14 ||
		si_chipid(bus->sih) == BCM4375_CHIP_ID ||
		si_chipid(bus->sih) == BCM4362_CHIP_ID ||
		si_chipid(bus->sih) == BCM43751_CHIP_ID ||
		si_chipid(bus->sih) == BCM4361_CHIP_ID ||
		si_chipid(bus->sih) == CYW55560_CHIP_ID) {
		return FALSE;
	} else {
		return TRUE;
	}
}

/**
 * Called once for each hardware (dongle) instance that this DHD manages.
 *
 * 'regs' is the host virtual address that maps to the start of the PCIe BAR0 window. The first 4096
 * bytes in this window are mapped to the backplane address in the PCIEBAR0Window register. The
 * precondition is that the PCIEBAR0Window register 'points' at the PCIe core.
 *
 * 'tcm' is the *host* virtual address at which tcm is mapped.
 */
int dhdpcie_bus_attach(osl_t *osh, dhd_bus_t **bus_ptr,
	volatile char *regs, volatile char *tcm, void *pci_dev)
{
	dhd_bus_t *bus = NULL;
	int ret = BCME_OK;
	/* customvar1 and customvar2 are customer configurable CIS tuples in OTP.
	* In dual chip (PCIE) scenario, customvar2 is used as a hint to detect
	* the chip variants and load the right firmware and NVRAM
	*/
	/* Below vars are set to 0x0 as OTPed value can not take 0x0 */
	uint32 customvar1 = 0x0;
	uint32 customvar2 = 0x0;
	uint32 otp_hw_module_variant = 0x0;
	DHD_TRACE(("%s: ENTER\n", __FUNCTION__));

	do {
		if (!(bus = MALLOCZ(osh, sizeof(dhd_bus_t)))) {
			DHD_ERROR(("%s: MALLOC of dhd_bus_t failed\n", __FUNCTION__));
			ret = BCME_NORESOURCE;
			break;
		}

		bus->regs = regs;
		bus->tcm = tcm;
		bus->osh = osh;
		/* Save pci_dev into dhd_bus, as it may be needed in dhd_attach */
		bus->dev = (struct pci_dev *)pci_dev;

		dll_init(&bus->flowring_active_list);
#ifdef IDLE_TX_FLOW_MGMT
		bus->active_list_last_process_ts = OSL_SYSUPTIME();
#endif /* IDLE_TX_FLOW_MGMT */

		/* Attach pcie shared structure */
		if (!(bus->pcie_sh = MALLOCZ(osh, sizeof(pciedev_shared_t)))) {
			DHD_ERROR(("%s: MALLOC of bus->pcie_sh failed\n", __FUNCTION__));
			ret = BCME_NORESOURCE;
			break;
		}

		/* dhd_common_init(osh); */

		if (dhdpcie_dongle_attach(bus)) {
			DHD_ERROR(("%s: dhdpcie_probe_attach failed\n", __FUNCTION__));
			ret = BCME_NOTREADY;
			break;
		}

		if (!hw_module_variant) {
			/* For single wifi module */
			goto enumerate_module;
		}

		/* read otp variable customvar and store in dhd->customvar1 and dhd->customvar2 */
		if (dhdpcie_sromotp_customvar(bus, &customvar1, &customvar2)) {
			DHD_ERROR(("%s: dhdpcie_sromotp_customvar failed\n", __FUNCTION__));
			break;
		}
		if (!customvar2) {
			DHD_ERROR(("%s:customvar2 is not OTPed"
				   "hw_module_variant=0x%x\n",
				   __FUNCTION__, hw_module_variant));
			goto enumerate_module;
		}
		/* customvar2=0xNNMMLLKK, LL is module variant */
		otp_hw_module_variant = (customvar2 >> 8) & 0xFF;
		DHD_TRACE(("%s hw_module_variant=0x%x and"
			"OTPed-module_variant=0x%x\n", __func__,
			 hw_module_variant, otp_hw_module_variant));
		if (hw_module_variant != otp_hw_module_variant) {
			DHD_ERROR(("%s: Not going to enumerate this module as "
				"hw_module_variant=0x%x and "
				"OTPed-module_variant=0x%x didn't match\n",
				__FUNCTION__, hw_module_variant, otp_hw_module_variant));
			break;
		}
		DHD_TRACE(("%s: Going to enumerate this module as "
			"hw_module_variant=0x%x and "
			"OTPed-module_variant=0x%x match\n",
			__FUNCTION__, hw_module_variant, otp_hw_module_variant));
enumerate_module:
		/* software resources */
		if (!(bus->dhd = dhd_attach(osh, bus, PCMSGBUF_HDRLEN))) {
			DHD_ERROR(("%s: dhd_attach failed\n", __FUNCTION__));
			break;
		}

		DHD_ERROR(("%s: making DHD_BUS_DOWN\n", __FUNCTION__));
		bus->dhd->busstate = DHD_BUS_DOWN;
		bus->dhd->hostrdy_after_init = TRUE;
		bus->db1_for_mb = TRUE;
		bus->dhd->hang_report = TRUE;
		bus->use_mailbox = FALSE;
		bus->use_d0_inform = FALSE;
		bus->intr_enabled = FALSE;
		bus->flr_force_fail = FALSE;
		/* By default disable HWA and enable it via iovar */
		bus->hwa_enab_bmap = 0;
		/* update the dma indices if set through module parameter. */
		if (dma_ring_indices != 0) {
			dhdpcie_set_dma_ring_indices(bus->dhd, dma_ring_indices);
		}
		/* update h2d phase support if set through module parameter */
		bus->dhd->h2d_phase_supported = h2d_phase ? TRUE : FALSE;
		/* update force trap on bad phase if set through module parameter */
		bus->dhd->force_dongletrap_on_bad_h2d_phase =
			force_trap_bad_h2d_phase ? TRUE : FALSE;
#ifdef IDLE_TX_FLOW_MGMT
		bus->enable_idle_flowring_mgmt = FALSE;
#endif /* IDLE_TX_FLOW_MGMT */
		bus->irq_registered = FALSE;

#ifdef DHD_MSI_SUPPORT
#ifdef DHD_FORCE_MSI
		bus->d2h_intr_method = PCIE_MSI;
#else
		bus->d2h_intr_method = enable_msi && dhdpcie_chip_support_msi(bus) ?
			PCIE_MSI : PCIE_INTX;
#endif /* DHD_FORCE_MSI */
#else
		bus->d2h_intr_method = PCIE_INTX;
#endif /* DHD_MSI_SUPPORT */

#ifdef DHD_HP2P
		bus->hp2p_txcpl_max_items = DHD_MAX_ITEMS_HPP_TXCPL_RING;
		bus->hp2p_rxcpl_max_items = DHD_MAX_ITEMS_HPP_RXCPL_RING;
#endif /* DHD_HP2P */

		DHD_TRACE(("%s: EXIT SUCCESS\n",
			__FUNCTION__));
		g_dhd_bus = bus;
		*bus_ptr = bus;
		return ret;
	} while (0);

	DHD_TRACE(("%s: EXIT FAILURE\n", __FUNCTION__));

	if (bus && bus->pcie_sh) {
		MFREE(osh, bus->pcie_sh, sizeof(pciedev_shared_t));
	}

	if (bus) {
		MFREE(osh, bus, sizeof(dhd_bus_t));
	}

	return ret;
}

bool
dhd_bus_skip_clm(dhd_pub_t *dhdp)
{
	switch (dhd_bus_chip_id(dhdp)) {
		case BCM4369_CHIP_ID:
			return TRUE;
		default:
			return FALSE;
	}
}

uint
dhd_bus_chip(struct dhd_bus *bus)
{
	ASSERT(bus->sih != NULL);
	return bus->sih->chip;
}

uint
dhd_bus_chiprev(struct dhd_bus *bus)
{
	ASSERT(bus);
	ASSERT(bus->sih != NULL);
	return bus->sih->chiprev;
}

void *
dhd_bus_pub(struct dhd_bus *bus)
{
	return bus->dhd;
}

void *
dhd_bus_sih(struct dhd_bus *bus)
{
	return (void *)bus->sih;
}

void *
dhd_bus_txq(struct dhd_bus *bus)
{
	return &bus->txq;
}

/** Get Chip ID version */
uint dhd_bus_chip_id(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = dhdp->bus;
	return  bus->sih->chip;
}

/** Get Chip Rev ID version */
uint dhd_bus_chiprev_id(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = dhdp->bus;
	return bus->sih->chiprev;
}

/** Get Chip Pkg ID version */
uint dhd_bus_chippkg_id(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = dhdp->bus;
	return bus->sih->chippkg;
}

/** Conduct Loopback test */
int
dhd_bus_dmaxfer_lpbk(dhd_pub_t *dhdp, uint32 type)
{
	dma_xfer_info_t dmaxfer_lpbk;
	int ret = BCME_OK;

#define PCIE_DMAXFER_LPBK_LENGTH	4096
	memset(&dmaxfer_lpbk, 0, sizeof(dma_xfer_info_t));
	dmaxfer_lpbk.version = DHD_DMAXFER_VERSION;
	dmaxfer_lpbk.length = (uint16)sizeof(dma_xfer_info_t);
	dmaxfer_lpbk.num_bytes = PCIE_DMAXFER_LPBK_LENGTH;
	dmaxfer_lpbk.type = type;
	dmaxfer_lpbk.should_wait = TRUE;

	ret = dhd_bus_iovar_op(dhdp, "pcie_dmaxfer", NULL, 0,
		(char *)&dmaxfer_lpbk, sizeof(dma_xfer_info_t), IOV_SET);
	if (ret < 0) {
		DHD_ERROR(("failed to start PCIe Loopback Test!!! "
			"Type:%d Reason:%d\n", type, ret));
		return ret;
	}

	if (dmaxfer_lpbk.status != DMA_XFER_SUCCESS) {
		DHD_ERROR(("failed to check PCIe Loopback Test!!! "
			"Type:%d Status:%d Error code:%d\n", type,
			dmaxfer_lpbk.status, dmaxfer_lpbk.error_code));
		ret = BCME_ERROR;
	} else {
		DHD_ERROR(("successful to check PCIe Loopback Test"
			" Type:%d\n", type));
	}
#undef PCIE_DMAXFER_LPBK_LENGTH

	return ret;
}

/* Log the lastest DPC schedule time */
void
dhd_bus_set_dpc_sched_time(dhd_pub_t *dhdp)
{
	dhdp->bus->dpc_sched_time = OSL_LOCALTIME_NS();
}

/* Check if there is DPC scheduling errors */
bool
dhd_bus_query_dpc_sched_errors(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = dhdp->bus;
	bool sched_err;

	if (bus->dpc_entry_time < bus->isr_exit_time) {
		/* Kernel doesn't schedule the DPC after processing PCIe IRQ */
		sched_err = TRUE;
	} else if (bus->dpc_entry_time < bus->resched_dpc_time) {
		/* Kernel doesn't schedule the DPC after DHD tries to reschedule
		 * the DPC due to pending work items to be processed.
		 */
		sched_err = TRUE;
	} else {
		sched_err = FALSE;
	}

	if (sched_err) {
		/* print out minimum timestamp info */
		DHD_ERROR(("isr_entry_time="SEC_USEC_FMT
			" isr_exit_time="SEC_USEC_FMT
			" dpc_entry_time="SEC_USEC_FMT
			"\ndpc_exit_time="SEC_USEC_FMT
			" dpc_sched_time="SEC_USEC_FMT
			" resched_dpc_time="SEC_USEC_FMT"\n",
			GET_SEC_USEC(bus->isr_entry_time),
			GET_SEC_USEC(bus->isr_exit_time),
			GET_SEC_USEC(bus->dpc_entry_time),
			GET_SEC_USEC(bus->dpc_exit_time),
			GET_SEC_USEC(bus->dpc_sched_time),
			GET_SEC_USEC(bus->resched_dpc_time)));
	}

	return sched_err;
}

/** Read and clear intstatus. This should be called with interrupts disabled or inside isr */
uint32
dhdpcie_bus_intstatus(dhd_bus_t *bus)
{
	uint32 intstatus = 0;
	uint32 intmask = 0;

	if (bus->bus_low_power_state == DHD_BUS_D3_ACK_RECIEVED) {
		DHD_ERROR(("%s: trying to clear intstatus after D3 Ack\n", __FUNCTION__));
		return intstatus;
	}
	if ((bus->sih->buscorerev == 6) || (bus->sih->buscorerev == 4) ||
		(bus->sih->buscorerev == 2)) {
		intstatus = dhdpcie_bus_cfg_read_dword(bus, PCIIntstatus, 4);
		dhdpcie_bus_cfg_write_dword(bus, PCIIntstatus, 4, intstatus);
		intstatus &= I_MB;
	} else {
		/* this is a PCIE core register..not a config register... */
		intstatus = si_corereg(bus->sih, bus->sih->buscoreidx, bus->pcie_mailbox_int, 0, 0);

		/* this is a PCIE core register..not a config register... */
		intmask = si_corereg(bus->sih, bus->sih->buscoreidx, bus->pcie_mailbox_mask, 0, 0);
		/* Is device removed. intstatus & intmask read 0xffffffff */
		if (intstatus == (uint32)-1 || intmask == (uint32)-1) {
			DHD_ERROR(("%s: Device is removed or Link is down.\n", __FUNCTION__));
			DHD_ERROR(("%s: INTSTAT : 0x%x INTMASK : 0x%x.\n",
			    __FUNCTION__, intstatus, intmask));
			bus->is_linkdown = TRUE;
			dhd_pcie_debug_info_dump(bus->dhd);
#ifdef CUSTOMER_HW4_DEBUG
#if defined(OEM_ANDROID)
#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
			bus->no_cfg_restore = 1;
#endif /* CONFIG_ARCH_MSM */
#endif /* SUPPORT_LINKDOWN_RECOVERY */
			bus->dhd->hang_reason = HANG_REASON_PCIE_LINK_DOWN_EP_DETECT;
			dhd_os_send_hang_message(bus->dhd);
#endif /* OEM_ANDROID */
#endif /* CUSTOMER_HW4_DEBUG */
			return intstatus;
		}

		intstatus &= intmask;

		/*
		 * The fourth argument to si_corereg is the "mask" fields of the register to update
		 * and the fifth field is the "value" to update. Now if we are interested in only
		 * few fields of the "mask" bit map, we should not be writing back what we read
		 * By doing so, we might clear/ack interrupts that are not handled yet.
		 */
		si_corereg(bus->sih, bus->sih->buscoreidx, bus->pcie_mailbox_int, bus->def_intmask,
			intstatus);

		intstatus &= bus->def_intmask;
	}

	return intstatus;
}

void
dhdpcie_cto_recovery_handler(dhd_pub_t *dhd)
{
	dhd_bus_t *bus = dhd->bus;
	int ret;

	/* Disable PCIe Runtime PM to avoid D3_ACK timeout.
	 */
	DHD_DISABLE_RUNTIME_PM(dhd);

	/* Sleep for 1 seconds so that any AXI timeout
	 * if running on ALP clock also will be captured
	 */
	OSL_SLEEP(1000);

	/* reset backplane and cto,
	 * then access through pcie is recovered.
	 */
	ret = dhdpcie_cto_error_recovery(bus);
	if (!ret) {
		/* Waiting for backplane reset */
		OSL_SLEEP(10);
		/* Dump debug Info */
		dhd_prot_debug_info_print(bus->dhd);
		/* Dump console buffer */
		dhd_bus_dump_console_buffer(bus);
#if defined(DHD_FW_COREDUMP)
		/* save core dump or write to a file */
		if (!bus->is_linkdown && bus->dhd->memdump_enabled) {
#ifdef DHD_SSSR_DUMP
			bus->dhd->collect_sssr = TRUE;
#endif /* DHD_SSSR_DUMP */
			bus->dhd->memdump_type = DUMP_TYPE_CTO_RECOVERY;
			dhdpcie_mem_dump(bus);
		}
#endif /* DHD_FW_COREDUMP */
	}
#ifdef OEM_ANDROID
#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
	bus->no_cfg_restore = 1;
#endif /* CONFIG_ARCH_MSM */
#endif /* SUPPORT_LINKDOWN_RECOVERY */
	bus->is_linkdown = TRUE;
	bus->dhd->hang_reason = HANG_REASON_PCIE_CTO_DETECT;
	/* Send HANG event */
	dhd_os_send_hang_message(bus->dhd);
#endif /* OEM_ANDROID */
}

/**
 * Name:  dhdpcie_bus_isr
 * Parameters:
 * 1: IN int irq   -- interrupt vector
 * 2: IN void *arg      -- handle to private data structure
 * Return value:
 * Status (TRUE or FALSE)
 *
 * Description:
 * Interrupt Service routine checks for the status register,
 * disable interrupt and queue DPC if mail box interrupts are raised.
 */
int32
dhdpcie_bus_isr(dhd_bus_t *bus)
{
	uint32 intstatus = 0;

	do {
		DHD_TRACE(("%s: Enter\n", __FUNCTION__));
		/* verify argument */
		if (!bus) {
			DHD_LOG_MEM(("%s : bus is null pointer, exit \n", __FUNCTION__));
			break;
		}

		if (bus->dhd->dongle_reset) {
			DHD_LOG_MEM(("%s : dongle is reset\n", __FUNCTION__));
			break;
		}

		if (bus->dhd->busstate == DHD_BUS_DOWN) {
			DHD_LOG_MEM(("%s : bus is down \n", __FUNCTION__));
			break;
		}

		/* avoid processing of interrupts until msgbuf prot is inited */
		if (!bus->intr_enabled) {
			DHD_INFO(("%s, not ready to receive interrupts\n", __FUNCTION__));
			break;
		}

		if (PCIECTO_ENAB(bus)) {
			/* read pci_intstatus */
			intstatus = dhdpcie_bus_cfg_read_dword(bus, PCI_INT_STATUS, 4);

			if (intstatus & PCI_CTO_INT_MASK) {
				DHD_ERROR(("%s: ##### CTO RECOVERY REPORTED BY DONGLE "
					"intstat=0x%x enab=%d\n", __FUNCTION__,
					intstatus, bus->cto_enable));
				bus->cto_triggered = 1;
				/*
				 * DAR still accessible
				 */
				dhd_bus_dump_dar_registers(bus);

				/* Disable further PCIe interrupts */
				dhdpcie_disable_irq_nosync(bus); /* Disable interrupt!! */
				/* Stop Tx flow */
				dhd_bus_stop_queue(bus);

				/* Schedule CTO recovery */
				dhd_schedule_cto_recovery(bus->dhd);

				return TRUE;
			}
		}

		if (bus->d2h_intr_method == PCIE_MSI) {
			/* For MSI, as intstatus is cleared by firmware, no need to read */
			goto skip_intstatus_read;
		}

		intstatus = dhdpcie_bus_intstatus(bus);

		/* Check if the interrupt is ours or not */
		if (intstatus == 0) {
			/* in EFI since we poll for interrupt, this message will flood the logs
			* so disable this for EFI
			*/
			DHD_LOG_MEM(("%s : this interrupt is not ours\n", __FUNCTION__));
			bus->non_ours_irq_count++;
			bus->last_non_ours_irq_time = OSL_LOCALTIME_NS();
			break;
		}

		/* save the intstatus */
		/* read interrupt status register!! Status bits will be cleared in DPC !! */
		bus->intstatus = intstatus;

		/* return error for 0xFFFFFFFF */
		if (intstatus == (uint32)-1) {
			DHD_LOG_MEM(("%s : wrong interrupt status val : 0x%x\n",
				__FUNCTION__, intstatus));
			dhdpcie_disable_irq_nosync(bus);
			break;
		}

skip_intstatus_read:
		/*  Overall operation:
		 *    - Mask further interrupts
		 *    - Read/ack intstatus
		 *    - Take action based on bits and state
		 *    - Reenable interrupts (as per state)
		 */

		/* Count the interrupt call */
		bus->intrcount++;

		bus->ipend = TRUE;

		bus->isr_intr_disable_count++;

		/* For Linux, Macos etc (otherthan NDIS) instead of disabling
		* dongle interrupt by clearing the IntMask, disable directly
		* interrupt from the host side, so that host will not recieve
		* any interrupts at all, even though dongle raises interrupts
		*/
		dhdpcie_disable_irq_nosync(bus); /* Disable interrupt!! */

		bus->intdis = TRUE;

#if defined(PCIE_ISR_THREAD)

		DHD_TRACE(("Calling dhd_bus_dpc() from %s\n", __FUNCTION__));
		DHD_OS_WAKE_LOCK(bus->dhd);
		while (dhd_bus_dpc(bus));
		DHD_OS_WAKE_UNLOCK(bus->dhd);
#else
		bus->dpc_sched = TRUE;
		dhd_sched_dpc(bus->dhd);     /* queue DPC now!! */
#endif /* defined(SDIO_ISR_THREAD) */

		DHD_TRACE(("%s: Exit Success DPC Queued\n", __FUNCTION__));
		return TRUE;

	} while (0);

	DHD_TRACE(("%s: Exit Failure\n", __FUNCTION__));
	return FALSE;
}

int
dhdpcie_set_pwr_state(dhd_bus_t *bus, uint state)
{
	uint32 cur_state = 0;
	uint32 pm_csr = 0;
	osl_t *osh = bus->osh;

	pm_csr = OSL_PCI_READ_CONFIG(osh, PCIECFGREG_PM_CSR, sizeof(uint32));
	cur_state = pm_csr & PCIECFGREG_PM_CSR_STATE_MASK;

	if (cur_state == state) {
		DHD_ERROR(("%s: Already in state %u \n", __FUNCTION__, cur_state));
		return BCME_OK;
	}

	if (state > PCIECFGREG_PM_CSR_STATE_D3_HOT)
		return BCME_ERROR;

	/* Validate the state transition
	* if already in a lower power state, return error
	*/
	if (state != PCIECFGREG_PM_CSR_STATE_D0 &&
			cur_state <= PCIECFGREG_PM_CSR_STATE_D3_COLD &&
			cur_state > state) {
		DHD_ERROR(("%s: Invalid power state transition !\n", __FUNCTION__));
		return BCME_ERROR;
	}

	pm_csr &= ~PCIECFGREG_PM_CSR_STATE_MASK;
	pm_csr |= state;

	OSL_PCI_WRITE_CONFIG(osh, PCIECFGREG_PM_CSR, sizeof(uint32), pm_csr);

	/* need to wait for the specified mandatory pcie power transition delay time */
	if (state == PCIECFGREG_PM_CSR_STATE_D3_HOT ||
			cur_state == PCIECFGREG_PM_CSR_STATE_D3_HOT)
			OSL_DELAY(DHDPCIE_PM_D3_DELAY);
	else if (state == PCIECFGREG_PM_CSR_STATE_D2 ||
			cur_state == PCIECFGREG_PM_CSR_STATE_D2)
			OSL_DELAY(DHDPCIE_PM_D2_DELAY);

	/* read back the power state and verify */
	pm_csr = OSL_PCI_READ_CONFIG(osh, PCIECFGREG_PM_CSR, sizeof(uint32));
	cur_state = pm_csr & PCIECFGREG_PM_CSR_STATE_MASK;
	if (cur_state != state) {
		DHD_ERROR(("%s: power transition failed ! Current state is %u \n",
				__FUNCTION__, cur_state));
		return BCME_ERROR;
	} else {
		DHD_ERROR(("%s: power transition to %u success \n",
				__FUNCTION__, cur_state));
	}

	return BCME_OK;
}

int
dhdpcie_config_check(dhd_bus_t *bus)
{
	uint32 i, val;
	int ret = BCME_ERROR;

	for (i = 0; i < DHDPCIE_CONFIG_CHECK_RETRY_COUNT; i++) {
		val = OSL_PCI_READ_CONFIG(bus->osh, PCI_CFG_VID, sizeof(uint32));
		if ((val & 0xFFFF) == VENDOR_BROADCOM || (val & 0xFFFF) == VENDOR_CYPRESS) {
			ret = BCME_OK;
			break;
		}
		OSL_DELAY(DHDPCIE_CONFIG_CHECK_DELAY_MS * 1000);
	}

	return ret;
}

int
dhdpcie_config_restore(dhd_bus_t *bus, bool restore_pmcsr)
{
	uint32 i;
	osl_t *osh = bus->osh;

	if (BCME_OK != dhdpcie_config_check(bus)) {
		return BCME_ERROR;
	}

	for (i = PCI_CFG_REV >> 2; i < DHDPCIE_CONFIG_HDR_SIZE; i++) {
		OSL_PCI_WRITE_CONFIG(osh, i << 2, sizeof(uint32), bus->saved_config.header[i]);
	}
	OSL_PCI_WRITE_CONFIG(osh, PCI_CFG_CMD, sizeof(uint32), bus->saved_config.header[1]);

	if (restore_pmcsr)
		OSL_PCI_WRITE_CONFIG(osh, PCIECFGREG_PM_CSR,
			sizeof(uint32), bus->saved_config.pmcsr);

	OSL_PCI_WRITE_CONFIG(osh, PCIECFGREG_MSI_CAP, sizeof(uint32), bus->saved_config.msi_cap);
	OSL_PCI_WRITE_CONFIG(osh, PCIECFGREG_MSI_ADDR_L, sizeof(uint32),
			bus->saved_config.msi_addr0);
	OSL_PCI_WRITE_CONFIG(osh, PCIECFGREG_MSI_ADDR_H,
			sizeof(uint32), bus->saved_config.msi_addr1);
	OSL_PCI_WRITE_CONFIG(osh, PCIECFGREG_MSI_DATA,
			sizeof(uint32), bus->saved_config.msi_data);

	OSL_PCI_WRITE_CONFIG(osh, PCIECFGREG_DEV_STATUS_CTRL,
			sizeof(uint32), bus->saved_config.exp_dev_ctrl_stat);
	OSL_PCI_WRITE_CONFIG(osh, PCIECFGGEN_DEV_STATUS_CTRL2,
			sizeof(uint32), bus->saved_config.exp_dev_ctrl_stat2);
	OSL_PCI_WRITE_CONFIG(osh, PCIECFGREG_LINK_STATUS_CTRL,
			sizeof(uint32), bus->saved_config.exp_link_ctrl_stat);
	OSL_PCI_WRITE_CONFIG(osh, PCIECFGREG_LINK_STATUS_CTRL2,
			sizeof(uint32), bus->saved_config.exp_link_ctrl_stat2);

	OSL_PCI_WRITE_CONFIG(osh, PCIECFGREG_PML1_SUB_CTRL1,
			sizeof(uint32), bus->saved_config.l1pm0);
	OSL_PCI_WRITE_CONFIG(osh, PCIECFGREG_PML1_SUB_CTRL2,
			sizeof(uint32), bus->saved_config.l1pm1);

	OSL_PCI_WRITE_CONFIG(bus->osh, PCI_BAR0_WIN, sizeof(uint32),
			bus->saved_config.bar0_win);
	dhdpcie_setbar1win(bus, bus->saved_config.bar1_win);

	return BCME_OK;
}

int
dhdpcie_config_save(dhd_bus_t *bus)
{
	uint32 i;
	osl_t *osh = bus->osh;

	if (BCME_OK != dhdpcie_config_check(bus)) {
		return BCME_ERROR;
	}

	for (i = 0; i < DHDPCIE_CONFIG_HDR_SIZE; i++) {
		bus->saved_config.header[i] = OSL_PCI_READ_CONFIG(osh, i << 2, sizeof(uint32));
	}

	bus->saved_config.pmcsr = OSL_PCI_READ_CONFIG(osh, PCIECFGREG_PM_CSR, sizeof(uint32));

	bus->saved_config.msi_cap = OSL_PCI_READ_CONFIG(osh, PCIECFGREG_MSI_CAP,
			sizeof(uint32));
	bus->saved_config.msi_addr0 = OSL_PCI_READ_CONFIG(osh, PCIECFGREG_MSI_ADDR_L,
			sizeof(uint32));
	bus->saved_config.msi_addr1 = OSL_PCI_READ_CONFIG(osh, PCIECFGREG_MSI_ADDR_H,
			sizeof(uint32));
	bus->saved_config.msi_data = OSL_PCI_READ_CONFIG(osh, PCIECFGREG_MSI_DATA,
			sizeof(uint32));

	bus->saved_config.exp_dev_ctrl_stat = OSL_PCI_READ_CONFIG(osh,
			PCIECFGREG_DEV_STATUS_CTRL, sizeof(uint32));
	bus->saved_config.exp_dev_ctrl_stat2 = OSL_PCI_READ_CONFIG(osh,
			PCIECFGGEN_DEV_STATUS_CTRL2, sizeof(uint32));
	bus->saved_config.exp_link_ctrl_stat = OSL_PCI_READ_CONFIG(osh,
			PCIECFGREG_LINK_STATUS_CTRL, sizeof(uint32));
	bus->saved_config.exp_link_ctrl_stat2 = OSL_PCI_READ_CONFIG(osh,
			PCIECFGREG_LINK_STATUS_CTRL2, sizeof(uint32));

	bus->saved_config.l1pm0 = OSL_PCI_READ_CONFIG(osh, PCIECFGREG_PML1_SUB_CTRL1,
			sizeof(uint32));
	bus->saved_config.l1pm1 = OSL_PCI_READ_CONFIG(osh, PCIECFGREG_PML1_SUB_CTRL2,
			sizeof(uint32));

	bus->saved_config.bar0_win = OSL_PCI_READ_CONFIG(osh, PCI_BAR0_WIN,
			sizeof(uint32));
	bus->saved_config.bar1_win = OSL_PCI_READ_CONFIG(osh, PCI_BAR1_WIN,
			sizeof(uint32));

	return BCME_OK;
}

#ifdef EXYNOS_PCIE_LINKDOWN_RECOVERY
dhd_pub_t *link_recovery = NULL;
#endif /* EXYNOS_PCIE_LINKDOWN_RECOVERY */

static void
dhdpcie_bus_intr_init(dhd_bus_t *bus)
{
	uint buscorerev = bus->sih->buscorerev;
	bus->pcie_mailbox_int = PCIMailBoxInt(buscorerev);
	bus->pcie_mailbox_mask = PCIMailBoxMask(buscorerev);
	bus->d2h_mb_mask = PCIE_MB_D2H_MB_MASK(buscorerev);
	bus->def_intmask = PCIE_MB_D2H_MB_MASK(buscorerev);
	if (buscorerev < 64) {
		bus->def_intmask |= PCIE_MB_TOPCIE_FN0_0 | PCIE_MB_TOPCIE_FN0_1;
	}
}

static void
dhdpcie_cc_watchdog_reset(dhd_bus_t *bus)
{
	uint32 wd_en = (bus->sih->buscorerev >= 66) ? WD_SSRESET_PCIE_F0_EN :
		(WD_SSRESET_PCIE_F0_EN | WD_SSRESET_PCIE_ALL_FN_EN);
	pcie_watchdog_reset(bus->osh, bus->sih, WD_ENABLE_MASK, wd_en);
}

void
dhdpcie_dongle_reset(dhd_bus_t *bus)
{
	/* if the pcie link is down, watchdog reset
	 * should not be done, as it may hang
	 */
	if (bus->is_linkdown) {
		return;
	}

	/* dhd_bus_perform_flr will return BCME_UNSUPPORTED if chip is not FLR capable */
	if (dhd_bus_perform_flr(bus, FALSE) == BCME_UNSUPPORTED) {
#ifdef DHD_USE_BP_RESET
		/* Backplane reset using SPROM cfg register(0x88) for buscorerev <= 24 */
		dhd_bus_perform_bp_reset(bus);
#else
		/* Legacy chipcommon watchdog reset */
		dhdpcie_cc_watchdog_reset(bus);
#endif /* DHD_USE_BP_RESET */
	}
}

#ifdef CHIPS_CUSTOMER_HW6
void
dhdpcie_bus_mpu_disable(dhd_bus_t *bus)
{
	volatile uint32 *cr4_regs;
	if (BCM4378_CHIP(bus->sih->chip)) {
		cr4_regs = si_setcore(bus->sih, ARMCR4_CORE_ID, 0);
		if (cr4_regs == NULL) {
			DHD_ERROR(("%s: Failed to find ARM core!\n", __FUNCTION__));
			return;
		}
		if (R_REG(bus->osh, cr4_regs + ARMCR4REG_CORECAP) & ACC_MPU_MASK) {
			/* bus mpu is supported */
			W_REG(bus->osh, cr4_regs + ARMCR4REG_MPUCTRL, 0);
		}
	}
}
#endif /* CHIPS_CUSTOMER_HW6 */

static bool
dhdpcie_dongle_attach(dhd_bus_t *bus)
{
	osl_t *osh = bus->osh;
	volatile void *regsva = (volatile void*)bus->regs;
	uint16 devid;
	uint32 val;
	uint32 reg_val = 0;
	bool is_pcie_reset = FALSE;
	uint32 secureboot;
	sbpcieregs_t *sbpcieregs;
	bool dongle_isolation;
	int32 bcmerror = BCME_ERROR;
	DHD_TRACE(("%s: ENTER\n", __FUNCTION__));

#ifdef EXYNOS_PCIE_LINKDOWN_RECOVERY
	link_recovery = bus->dhd;
#endif /* EXYNOS_PCIE_LINKDOWN_RECOVERY */

	bus->alp_only = TRUE;
	bus->sih = NULL;

	/* Checking PCIe bus status with reading configuration space */
	val = OSL_PCI_READ_CONFIG(osh, PCI_CFG_VID, sizeof(uint32));
	if ((val & 0xFFFF) != VENDOR_BROADCOM && (val & 0xFFFF) != VENDOR_CYPRESS) {
		DHD_ERROR(("%s : failed to read PCI configuration space!\n", __FUNCTION__));
		goto fail;
	}
	devid = (val >> 16) & 0xFFFF;
	bus->cl_devid = devid;

	/* Set bar0 window to si_enum_base */
	dhdpcie_bus_cfg_set_bar0_win(bus, si_enum_base(devid));

	/*
	 * Checking PCI_SPROM_CONTROL register for preventing invalid address access
	 * due to switch address space from PCI_BUS to SI_BUS.
	 */
	val = OSL_PCI_READ_CONFIG(osh, PCI_SPROM_CONTROL, sizeof(uint32));
	if (val == 0xffffffff) {
		DHD_ERROR(("%s : failed to read SPROM control register\n", __FUNCTION__));
		goto fail;
	}

	/* Getting Secureboot capability to make sure that the
	 * functionalities are ristricted to the chips having bootloader
	 */
	secureboot = OSL_PCI_READ_CONFIG(osh, PCIECFGREG_REVID, sizeof(uint32));

	if (isset(&secureboot, PCIECFGREG_SECURE_MODE_SHIFT)) {

		/* Set bar0 window to si_pcie_enum_base */
		dhdpcie_bus_cfg_set_bar0_win(bus, si_pcie_enum_base(devid));
		sbpcieregs = (sbpcieregs_t*)(bus->regs);
		DHD_INFO(("%s: before read reg_val:%d\n", __FUNCTION__, reg_val));
		reg_val = R_REG(osh, &sbpcieregs->u1.dar_64.d2h_msg_reg0);
		DHD_INFO(("%s: after reg_val:%d\n", __FUNCTION__, reg_val));
		if (reg_val != D2H_HS_START_STATE || reg_val != (D2H_HS_READY_STATE)) {
		/* si_attach() will provide an SI handle and scan the backplane */
			if (!(bus->sih = si_attach((uint)devid, osh, regsva, PCI_BUS, bus,
			     &bus->vars, &bus->varsz))) {
				DHD_ERROR(("%s: si_attach failed!\n", __FUNCTION__));
				goto fail;
			}
			dhdpcie_dongle_reset(bus);
			is_pcie_reset = TRUE;
		}

		/* Pre ChipID access sequence, make sure that
		 * bootloader is ready before ChipID access.
		 */
		bcmerror = dhdpcie_dongle_host_pre_chipid_access_sequence(osh, regsva);
		if (bcmerror) {
			DHD_ERROR(("%s: error - pre chipid access sequence error %d\n",
				__FUNCTION__, bcmerror));
			goto fail;
		}

		/* Set bar0 window to si_enum_base */
		dhdpcie_bus_cfg_set_bar0_win(bus, si_enum_base(devid));
	}

	/* si_attach() will provide an SI handle and scan the backplane */
	if (!(bus->sih = si_attach((uint)devid, osh, regsva, PCI_BUS, bus,
	                           &bus->vars, &bus->varsz))) {
		DHD_ERROR(("%s: si_attach failed!\n", __FUNCTION__));
		goto fail;
	}

	/* Configure CTO Prevention functionality */
#if defined(BCMFPGA_HW)
	DHD_ERROR(("Disable CTO\n"));
	bus->cto_enable = FALSE;
#else
#if defined(BCMPCIE_CTO_PREVENTION)
	if (bus->sih->buscorerev >= 24) {
		DHD_ERROR(("Enable CTO\n"));
		bus->cto_enable = TRUE;
	} else
#endif /* BCMPCIE_CTO_PREVENTION */
	{
		DHD_ERROR(("Disable CTO\n"));
		bus->cto_enable = FALSE;
	}
#endif /* BCMFPGA_HW */

	if (PCIECTO_ENAB(bus)) {
		dhdpcie_cto_init(bus, TRUE);
	}

	/* Storing secureboot capability */
	bus->sih->secureboot = isset(&secureboot, PCIECFGREG_SECURE_MODE_SHIFT);

	if (MULTIBP_ENAB(bus->sih) && (bus->sih->buscorerev >= 66)) {
		/*
		 * HW JIRA - CRWLPCIEGEN2-672
		 * Producer Index Feature which is used by F1 gets reset on F0 FLR
		 * fixed in REV68
		 */
		if (PCIE_ENUM_RESET_WAR_ENAB(bus->sih->buscorerev)) {
			dhdpcie_ssreset_dis_enum_rst(bus);
		}

		/* IOV_DEVRESET could exercise si_detach()/si_attach() again so reset
		*   dhdpcie_bus_release_dongle() --> si_detach()
		*   dhdpcie_dongle_attach() --> si_attach()
		*/
		bus->pwr_req_ref = 0;
	}

	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req_nolock(bus);
	}

	/* Get info on the ARM and SOCRAM cores... */
	/* Should really be qualified by device id */
	if ((si_setcore(bus->sih, ARM7S_CORE_ID, 0)) ||
	    (si_setcore(bus->sih, ARMCM3_CORE_ID, 0)) ||
	    (si_setcore(bus->sih, ARMCR4_CORE_ID, 0)) ||
	    (si_setcore(bus->sih, ARMCA7_CORE_ID, 0))) {
		bus->armrev = si_corerev(bus->sih);
		bus->coreid = si_coreid(bus->sih);
	} else {
		DHD_ERROR(("%s: failed to find ARM core!\n", __FUNCTION__));
		goto fail;
	}

	/* CA7 requires coherent bits on */
	if (bus->coreid == ARMCA7_CORE_ID) {
		val = dhdpcie_bus_cfg_read_dword(bus, PCIE_CFG_SUBSYSTEM_CONTROL, 4);
		dhdpcie_bus_cfg_write_dword(bus, PCIE_CFG_SUBSYSTEM_CONTROL, 4,
			(val | PCIE_BARCOHERENTACCEN_MASK));
	}

	/* Olympic EFI requirement - stop driver load if FW is already running
	*  need to do this here before pcie_watchdog_reset, because
	*  pcie_watchdog_reset will put the ARM back into halt state
	*/
	if (!dhdpcie_is_arm_halted(bus)) {
		DHD_ERROR(("%s: ARM is not halted,FW is already running! Abort.\n",
				__FUNCTION__));
		goto fail;
	}

	BCM_REFERENCE(dongle_isolation);

	/* For inbuilt drivers pcie clk req will be done by RC,
	 * so do not do clkreq from dhd
	 */
	if (dhd_download_fw_on_driverload)
	{
		/* Enable CLKREQ# */
		dhdpcie_clkreq(bus->osh, 1, 1);
	}

	/*
	 * bus->dhd will be NULL if it is called from dhd_bus_attach, so need to reset
	 * without checking dongle_isolation flag, but if it is called via some other path
	 * like quiesce FLR, then based on dongle_isolation flag, watchdog_reset should
	 * be called.
	 */
	if (bus->dhd == NULL) {
		/* dhd_attach not yet happened, do watchdog reset */
		dongle_isolation = FALSE;
	} else {
		dongle_isolation = bus->dhd->dongle_isolation;
	}

#ifndef DHD_SKIP_DONGLE_RESET_IN_ATTACH
	/*
	 * Issue CC watchdog to reset all the cores on the chip - similar to rmmod dhd
	 * This is required to avoid spurious interrupts to the Host and bring back
	 * dongle to a sane state (on host soft-reboot / watchdog-reboot).
	 */
	if (dongle_isolation == FALSE && is_pcie_reset == FALSE) {
		dhdpcie_dongle_reset(bus);
	}
#endif /* !DHD_SKIP_DONGLE_RESET_IN_ATTACH */

	/* need to set the force_bt_quiesce flag here
	 * before calling dhdpcie_dongle_flr_or_pwr_toggle
	 */
	bus->force_bt_quiesce = TRUE;
	/*
	 * For buscorerev = 66 and after, F0 FLR should be done independent from F1.
	 * So don't need BT quiesce.
	 */
	if (bus->sih->buscorerev >= 66) {
		bus->force_bt_quiesce = FALSE;
	}

	dhdpcie_dongle_flr_or_pwr_toggle(bus);

#ifdef CHIPS_CUSTOMER_HW6
	dhdpcie_bus_mpu_disable(bus);
#endif /* CHIPS_CUSTOMER_HW6 */

	si_setcore(bus->sih, PCIE2_CORE_ID, 0);
	sbpcieregs = (sbpcieregs_t*)(bus->regs);

	/* WAR where the BAR1 window may not be sized properly */
	W_REG(osh, &sbpcieregs->configaddr, 0x4e0);
	val = R_REG(osh, &sbpcieregs->configdata);
	W_REG(osh, &sbpcieregs->configdata, val);

	if (si_setcore(bus->sih, SYSMEM_CORE_ID, 0)) {
		/* Only set dongle RAMSIZE to default value when BMC vs ARM usage of SYSMEM is not
		 * adjusted.
		 */
		if (!bus->ramsize_adjusted) {
			if (!(bus->orig_ramsize = si_sysmem_size(bus->sih))) {
				DHD_ERROR(("%s: failed to find SYSMEM memory!\n", __FUNCTION__));
				goto fail;
			}
			switch ((uint16)bus->sih->chip) {
#ifdef CHIPS_CUSTOMER_HW6
				case BCM4368_CHIP_ID:
					bus->dongle_ram_base = CA7_4368_RAM_BASE;
					bus->orig_ramsize = 0x1c0000;
					break;
				CASE_BCM4367_CHIP:
					bus->dongle_ram_base = CA7_4367_RAM_BASE;
					bus->orig_ramsize = 0x1e0000;
					break;
#endif /* CHIPS_CUSTOMER_HW6 */
				default:
					/* also populate base address */
					bus->dongle_ram_base = CA7_4365_RAM_BASE;
					bus->orig_ramsize = 0x1c0000; /* Reserve 1.75MB for CA7 */
					break;
			}
		}
	} else if (!si_setcore(bus->sih, ARMCR4_CORE_ID, 0)) {
		if (!(bus->orig_ramsize = si_socram_size(bus->sih))) {
			DHD_ERROR(("%s: failed to find SOCRAM memory!\n", __FUNCTION__));
			goto fail;
		}
	} else {
		/* cr4 has a different way to find the RAM size from TCM's */
		if (!(bus->orig_ramsize = si_tcm_size(bus->sih))) {
			DHD_ERROR(("%s: failed to find CR4-TCM memory!\n", __FUNCTION__));
			goto fail;
		}
		/* also populate base address */
		switch ((uint16)bus->sih->chip) {
		case BCM4339_CHIP_ID:
		case BCM4335_CHIP_ID:
			bus->dongle_ram_base = CR4_4335_RAM_BASE;
			break;
		case BCM4358_CHIP_ID:
		case BCM4354_CHIP_ID:
		case BCM43567_CHIP_ID:
		case BCM43569_CHIP_ID:
		case BCM4350_CHIP_ID:
		case BCM43570_CHIP_ID:
			bus->dongle_ram_base = CR4_4350_RAM_BASE;
			break;
		case BCM4360_CHIP_ID:
			bus->dongle_ram_base = CR4_4360_RAM_BASE;
			break;

		case BCM4364_CHIP_ID:
			bus->dongle_ram_base = CR4_4364_RAM_BASE;
			break;

		CASE_BCM4345_CHIP:
			bus->dongle_ram_base = (bus->sih->chiprev < 6)  /* changed at 4345C0 */
				? CR4_4345_LT_C0_RAM_BASE : CR4_4345_GE_C0_RAM_BASE;
			break;
		CASE_BCM43602_CHIP:
			bus->dongle_ram_base = CR4_43602_RAM_BASE;
			break;
		case BCM4349_CHIP_GRPID:
			/* RAM based changed from 4349c0(revid=9) onwards */
			bus->dongle_ram_base = ((bus->sih->chiprev < 9) ?
				CR4_4349_RAM_BASE : CR4_4349_RAM_BASE_FROM_REV_9);
			break;
		case BCM4347_CHIP_ID:
		case BCM4357_CHIP_ID:
		case BCM4361_CHIP_ID:
			bus->dongle_ram_base = CR4_4347_RAM_BASE;
			break;
		case BCM4362_CHIP_ID:
			bus->dongle_ram_base = CR4_4362_RAM_BASE;
			break;
		case BCM43751_CHIP_ID:
			bus->dongle_ram_base = CR4_43751_RAM_BASE;
			break;

		case BCM4373_CHIP_ID:
			bus->dongle_ram_base = CR4_4373_RAM_BASE;
			break;
#ifdef CHIPS_CUSTOMER_HW6
		case BCM4378_CHIP_GRPID:
			bus->dongle_ram_base = CR4_4378_RAM_BASE;
			break;
		case BCM4377_CHIP_ID:
			bus->dongle_ram_base = CR4_4377_RAM_BASE;
			break;
#endif /* CHIPS_CUSTOMER_HW6 */
		case BCM4375_CHIP_ID:
		case BCM4369_CHIP_ID:
			bus->dongle_ram_base = CR4_4369_RAM_BASE;
			break;
		case CYW55560_CHIP_ID:
			bus->dongle_ram_base = CR4_55560_RAM_BASE;
			break;
		default:
			bus->dongle_ram_base = 0;
			DHD_ERROR(("%s: WARNING: Using default ram base at 0x%x\n",
			           __FUNCTION__, bus->dongle_ram_base));
		}
	}

	/* 55560, Dedicated space for TCAM patching and TRX Hader at RAMBASE */
	/* TCAM Patching - 2048[2K], TRX Header - 32Bytes */
	if (bus->sih->chip == CYW55560_CHIP_ID) {
		bus->orig_ramsize -= (CR4_55560_TCAM_SZ + CR4_55560_TRX_HDR_SZ);
	}

	bus->ramsize = bus->orig_ramsize;
	if (dhd_dongle_memsize)
		dhdpcie_bus_dongle_setmemsize(bus, dhd_dongle_memsize);

	if (bus->ramsize > DONGLE_TCM_MAP_SIZE) {
		DHD_ERROR(("%s : invalid ramsize %d(0x%x) is returned from dongle\n",
				__FUNCTION__, bus->ramsize, bus->ramsize));
		goto fail;
	}

	DHD_ERROR(("DHD: dongle ram size is set to %d(orig %d) at 0x%x\n",
	           bus->ramsize, bus->orig_ramsize, bus->dongle_ram_base));

	bus->srmemsize = si_socram_srmem_size(bus->sih);

	dhdpcie_bus_intr_init(bus);

	/* Set the poll and/or interrupt flags */
	bus->intr = (bool)dhd_intr;

	bus->idma_enabled = TRUE;
	bus->ifrm_enabled = TRUE;
	DHD_TRACE(("%s: EXIT: SUCCESS\n", __FUNCTION__));

	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req_clear_nolock(bus);

		/*
		 * One time clearing of Common Power Domain since HW default is set
		 * Needs to be after FLR because FLR resets PCIe enum back to HW defaults
		 * for 4378B0 (rev 68).
		 * On 4378A0 (rev 66), PCIe enum reset is disabled due to CRWLPCIEGEN2-672
		 */
		si_srpwr_request(bus->sih, SRPWR_DMN0_PCIE_MASK, 0);

		/*
		 * WAR to fix ARM cold boot;
		 * Assert WL domain in DAR helps but not enum
		 */
		if (bus->sih->buscorerev >= 68) {
			dhd_bus_pcie_pwr_req_wl_domain(bus, TRUE);
		}
	}

	return 0;

fail:
	if (bus->sih != NULL) {
		if (MULTIBP_ENAB(bus->sih)) {
			dhd_bus_pcie_pwr_req_clear_nolock(bus);
		}
		/* for EFI even if there is an error, load still succeeds
		* so si_detach should not be called here, it is called during unload
		*/
		si_detach(bus->sih);
		bus->sih = NULL;
	}
	DHD_TRACE(("%s: EXIT: FAILURE\n", __FUNCTION__));
	return -1;
}

int
dhpcie_bus_unmask_interrupt(dhd_bus_t *bus)
{
	dhdpcie_bus_cfg_write_dword(bus, PCIIntmask, 4, I_MB);
	return 0;
}
int
dhpcie_bus_mask_interrupt(dhd_bus_t *bus)
{
	dhdpcie_bus_cfg_write_dword(bus, PCIIntmask, 4, 0x0);
	return 0;
}

/* Non atomic function, caller should hold appropriate lock */
void
dhdpcie_bus_intr_enable(dhd_bus_t *bus)
{
	DHD_TRACE(("%s Enter\n", __FUNCTION__));
	if (bus) {
		if (bus->sih && !bus->is_linkdown) {
			/* Skip after recieving D3 ACK */
			if (bus->bus_low_power_state == DHD_BUS_D3_ACK_RECIEVED) {
				return;
			}
			if ((bus->sih->buscorerev == 2) || (bus->sih->buscorerev == 6) ||
				(bus->sih->buscorerev == 4)) {
				dhpcie_bus_unmask_interrupt(bus);
			} else {
	#if defined(BCMINTERNAL) && defined(DHD_DBG_DUMP)
				dhd_bus_mmio_trace(bus, bus->pcie_mailbox_mask,
					bus->def_intmask, TRUE);
	#endif
				si_corereg(bus->sih, bus->sih->buscoreidx, bus->pcie_mailbox_mask,
					bus->def_intmask, bus->def_intmask);
			}
		}

	}

	DHD_TRACE(("%s Exit\n", __FUNCTION__));
}

/* Non atomic function, caller should hold appropriate lock */
void
dhdpcie_bus_intr_disable(dhd_bus_t *bus)
{
	DHD_TRACE(("%s Enter\n", __FUNCTION__));
	if (bus && bus->sih && !bus->is_linkdown) {
		/* Skip after recieving D3 ACK */
		if (bus->bus_low_power_state == DHD_BUS_D3_ACK_RECIEVED) {
			return;
		}
		if ((bus->sih->buscorerev == 2) || (bus->sih->buscorerev == 6) ||
			(bus->sih->buscorerev == 4)) {
			dhpcie_bus_mask_interrupt(bus);
		} else {
			si_corereg(bus->sih, bus->sih->buscoreidx, bus->pcie_mailbox_mask,
				bus->def_intmask, 0);
		}
	}

	DHD_TRACE(("%s Exit\n", __FUNCTION__));
}

/*
 *  dhdpcie_advertise_bus_cleanup advertises that clean up is under progress
 * to other bus user contexts like Tx, Rx, IOVAR, WD etc and it waits for other contexts
 * to gracefully exit. All the bus usage contexts before marking busstate as busy, will check for
 * whether the busstate is DHD_BUS_DOWN or DHD_BUS_DOWN_IN_PROGRESS, if so
 * they will exit from there itself without marking dhd_bus_busy_state as BUSY.
 */
void
dhdpcie_advertise_bus_cleanup(dhd_pub_t	 *dhdp)
{
	unsigned long flags;
	int timeleft;

#ifdef DHD_PCIE_RUNTIMEPM
	dhdpcie_runtime_bus_wake(dhdp, TRUE, dhdpcie_advertise_bus_cleanup);
#endif /* DHD_PCIE_RUNTIMEPM */

	dhdp->dhd_watchdog_ms_backup = dhd_watchdog_ms;
	if (dhdp->dhd_watchdog_ms_backup) {
		DHD_ERROR(("%s: Disabling wdtick before dhd deinit\n",
			__FUNCTION__));
		dhd_os_wd_timer(dhdp, 0);
	}
	if (dhdp->busstate != DHD_BUS_DOWN) {
		DHD_GENERAL_LOCK(dhdp, flags);
		dhdp->busstate = DHD_BUS_DOWN_IN_PROGRESS;
		DHD_GENERAL_UNLOCK(dhdp, flags);
	}

	timeleft = dhd_os_busbusy_wait_negation(dhdp, &dhdp->dhd_bus_busy_state);
	if ((timeleft == 0) || (timeleft == 1)) {
		DHD_ERROR(("%s : Timeout due to dhd_bus_busy_state=0x%x\n",
				__FUNCTION__, dhdp->dhd_bus_busy_state));
		ASSERT(0);
	}

	return;
}

static void
dhdpcie_bus_remove_prep(dhd_bus_t *bus)
{
	unsigned long flags;
	DHD_TRACE(("%s Enter\n", __FUNCTION__));

	DHD_GENERAL_LOCK(bus->dhd, flags);
	DHD_ERROR(("%s: making DHD_BUS_DOWN\n", __FUNCTION__));
	bus->dhd->busstate = DHD_BUS_DOWN;
	DHD_GENERAL_UNLOCK(bus->dhd, flags);

	dhd_os_sdlock(bus->dhd);

	if (bus->sih && !bus->dhd->dongle_isolation) {
		if (PCIE_RELOAD_WAR_ENAB(bus->sih->buscorerev) &&
		    bus->sih->chip != CYW55560_CHIP_ID) {
			dhd_bus_pcie_pwr_req_reload_war(bus);
		}

		/* Has insmod fails after rmmod issue in Brix Android */
#if !defined(OEM_ANDROID) && !defined(ANDROID)
		/* HW4347-909 */
		if ((bus->sih->buscorerev == 19) || (bus->sih->buscorerev == 23)) {
			/* Set PCIE TRefUp time to 100us for 4347 */
			pcie_set_trefup_time_100us(bus->sih);
		}

		/* disable fast lpo from 4347 */
		/* For 4378/4387, do not disable fast lpo because we always enable fast lpo.
		 * it causes insmod/rmmod reload failure.
		 */
		if ((PMUREV(bus->sih->pmurev) > 31) &&
		    (bus->sih->buscorerev != 66) &&
		    (bus->sih->buscorerev != 68) &&
		    (bus->sih->buscorerev != 69) &&
		    (bus->sih->buscorerev != 70)) {
			si_pmu_fast_lpo_disable(bus->sih);
		}
#endif /* !OEM_ANDROID && !ANDROID */

		/* if the pcie link is down, watchdog reset
		* should not be done, as it may hang
		*/

		if (!bus->is_linkdown) {
#ifndef DHD_SKIP_DONGLE_RESET_IN_ATTACH
			/* for efi, depending on bt over pcie mode
			*  we either power toggle or do F0 FLR
			* from dhdpcie_bus_release dongle. So no need to
			* do dongle reset from here
			*/
			dhdpcie_dongle_reset(bus);
#endif /* !DHD_SKIP_DONGLE_RESET_IN_ATTACH */
		}

		bus->dhd->is_pcie_watchdog_reset = TRUE;
	}

	dhd_os_sdunlock(bus->dhd);

	DHD_TRACE(("%s Exit\n", __FUNCTION__));
}

void
dhd_init_bus_lock(dhd_bus_t *bus)
{
	if (!bus->bus_lock) {
		bus->bus_lock = dhd_os_spin_lock_init(bus->dhd->osh);
	}
}

void
dhd_deinit_bus_lock(dhd_bus_t *bus)
{
	if (bus->bus_lock) {
		dhd_os_spin_lock_deinit(bus->dhd->osh, bus->bus_lock);
		bus->bus_lock = NULL;
	}
}

void
dhd_init_backplane_access_lock(dhd_bus_t *bus)
{
	if (!bus->backplane_access_lock) {
		bus->backplane_access_lock = dhd_os_spin_lock_init(bus->dhd->osh);
	}
}

void
dhd_deinit_backplane_access_lock(dhd_bus_t *bus)
{
	if (bus->backplane_access_lock) {
		dhd_os_spin_lock_deinit(bus->dhd->osh, bus->backplane_access_lock);
		bus->backplane_access_lock = NULL;
	}
}

/** Detach and free everything */
void
dhdpcie_bus_release(dhd_bus_t *bus)
{
	bool dongle_isolation = FALSE;
#ifdef BCMQT
	uint buscorerev = 0;
#endif /* BCMQT */
	osl_t *osh = NULL;
	unsigned long flags_bus;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus) {

		osh = bus->osh;
		ASSERT(osh);

		if (bus->dhd) {
#if defined(DEBUGGER) || defined(DHD_DSCOPE)
			debugger_close();
#endif /* DEBUGGER || DHD_DSCOPE */
			dhdpcie_advertise_bus_cleanup(bus->dhd);
			dongle_isolation = bus->dhd->dongle_isolation;
			bus->dhd->is_pcie_watchdog_reset = FALSE;
			dhdpcie_bus_remove_prep(bus);

			if (bus->intr) {
				DHD_BUS_LOCK(bus->bus_lock, flags_bus);
				dhdpcie_bus_intr_disable(bus);
				DHD_BUS_UNLOCK(bus->bus_lock, flags_bus);
				dhdpcie_free_irq(bus);
			}
			dhd_deinit_bus_lock(bus);
			dhd_deinit_backplane_access_lock(bus);
#ifdef BCMQT
			if (IDMA_ACTIVE(bus->dhd)) {
			/**
			 * On FPGA during exit path force set "IDMA Control Register"
			 * to default value 0x0. Otherwise host dongle syc for IDMA fails
			 * during next IDMA initilization(without system reboot)
			 */
				buscorerev = bus->sih->buscorerev;
				si_corereg(bus->sih, bus->sih->buscoreidx,
					IDMAControl(buscorerev), ~0, 0);
			}
#endif /* BCMQT */
			/**
			 * dhdpcie_bus_release_dongle free bus->sih  handle, which is needed to
			 * access Dongle registers.
			 * dhd_detach will communicate with dongle to delete flowring ..etc.
			 * So dhdpcie_bus_release_dongle should be called only after the dhd_detach.
			 */
			dhd_detach(bus->dhd);
			dhdpcie_bus_release_dongle(bus, osh, dongle_isolation, TRUE);
			dhd_free(bus->dhd);
			bus->dhd = NULL;
		}
		/* unmap the regs and tcm here!! */
		if (bus->regs) {
			dhdpcie_bus_reg_unmap(osh, bus->regs, DONGLE_REG_MAP_SIZE);
			bus->regs = NULL;
		}
		if (bus->tcm) {
			dhdpcie_bus_reg_unmap(osh, bus->tcm, DONGLE_TCM_MAP_SIZE);
			bus->tcm = NULL;
		}

		dhdpcie_bus_release_malloc(bus, osh);
		/* Detach pcie shared structure */
		if (bus->pcie_sh) {
			MFREE(osh, bus->pcie_sh, sizeof(pciedev_shared_t));
			bus->pcie_sh = NULL;
		}

		if (bus->console.buf != NULL) {
			MFREE(osh, bus->console.buf, bus->console.bufsize);
		}

		/* Finally free bus info */
		MFREE(osh, bus, sizeof(dhd_bus_t));

		g_dhd_bus = NULL;
	}

	DHD_TRACE(("%s: Exit\n", __FUNCTION__));
} /* dhdpcie_bus_release */

void
dhdpcie_bus_release_dongle(dhd_bus_t *bus, osl_t *osh, bool dongle_isolation, bool reset_flag)
{
	DHD_TRACE(("%s: Enter bus->dhd %p bus->dhd->dongle_reset %d \n", __FUNCTION__,
		bus->dhd, bus->dhd->dongle_reset));

	if ((bus->dhd && bus->dhd->dongle_reset) && reset_flag) {
		DHD_TRACE(("%s Exit\n", __FUNCTION__));
		return;
	}

	if (bus->is_linkdown) {
		DHD_ERROR(("%s : Skip release dongle due to linkdown \n", __FUNCTION__));
		return;
	}

	if (bus->sih) {

		if (!dongle_isolation &&
			(bus->dhd && !bus->dhd->is_pcie_watchdog_reset)) {
			dhdpcie_dongle_reset(bus);
		}

		dhdpcie_dongle_flr_or_pwr_toggle(bus);

		if (bus->ltrsleep_on_unload) {
			si_corereg(bus->sih, bus->sih->buscoreidx,
				OFFSETOF(sbpcieregs_t, u.pcie2.ltr_state), ~0, 0);
		}

		if (bus->sih->buscorerev == 13)
			 pcie_serdes_iddqdisable(bus->osh, bus->sih,
			                         (sbpcieregs_t *) bus->regs);

		/* For inbuilt drivers pcie clk req will be done by RC,
		 * so do not do clkreq from dhd
		 */
		if (dhd_download_fw_on_driverload)
		{
			/* Disable CLKREQ# */
			dhdpcie_clkreq(bus->osh, 1, 0);
		}

#ifdef PCIE_SUSPEND_DURING_DETACH
		dhdpcie_bus_clock_stop(bus);
#endif /* PCIE_SUSPEND_DURING_DETACH */

		if (bus->sih != NULL) {
			si_detach(bus->sih);
			bus->sih = NULL;
		}
		if (bus->vars && bus->varsz)
			MFREE(osh, bus->vars, bus->varsz);
		bus->vars = NULL;
	}

	DHD_TRACE(("%s Exit\n", __FUNCTION__));
}

uint32
dhdpcie_bus_cfg_read_dword(dhd_bus_t *bus, uint32 addr, uint32 size)
{
	uint32 data = OSL_PCI_READ_CONFIG(bus->osh, addr, size);
	return data;
}

/** 32 bit config write */
void
dhdpcie_bus_cfg_write_dword(dhd_bus_t *bus, uint32 addr, uint32 size, uint32 data)
{
	OSL_PCI_WRITE_CONFIG(bus->osh, addr, size, data);
}

void
dhdpcie_bus_cfg_set_bar0_win(dhd_bus_t *bus, uint32 data)
{
	OSL_PCI_WRITE_CONFIG(bus->osh, PCI_BAR0_WIN, 4, data);
}

void
dhdpcie_bus_dongle_setmemsize(struct dhd_bus *bus, int mem_size)
{
	int32 min_size =  DONGLE_MIN_MEMSIZE;
	/* Restrict the memsize to user specified limit */
	DHD_ERROR(("user: Restrict the dongle ram size to %d, min accepted %d\n",
		dhd_dongle_memsize, min_size));
	if ((dhd_dongle_memsize > min_size) &&
		(dhd_dongle_memsize < (int32)bus->orig_ramsize))
		bus->ramsize = dhd_dongle_memsize;
}

void
dhdpcie_bus_release_malloc(dhd_bus_t *bus, osl_t *osh)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus->dhd && bus->dhd->dongle_reset)
		return;

	if (bus->vars && bus->varsz) {
		MFREE(osh, bus->vars, bus->varsz);
		bus->vars = NULL;
	}

	DHD_TRACE(("%s: Exit\n", __FUNCTION__));
	return;

}

/** Stop bus module: clear pending frames, disable data flow */
void dhd_bus_stop(struct dhd_bus *bus, bool enforce_mutex)
{
	unsigned long flags, flags_bus;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (!bus->dhd)
		return;

	if (bus->dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s: already down by net_dev_reset\n", __FUNCTION__));
		goto done;
	}

	DHD_DISABLE_RUNTIME_PM(bus->dhd);

	DHD_GENERAL_LOCK(bus->dhd, flags);
	DHD_ERROR(("%s: making DHD_BUS_DOWN\n", __FUNCTION__));
	bus->dhd->busstate = DHD_BUS_DOWN;
	DHD_GENERAL_UNLOCK(bus->dhd, flags);

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	atomic_set(&bus->dhd->block_bus, TRUE);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

	DHD_BUS_LOCK(bus->bus_lock, flags_bus);
	dhdpcie_bus_intr_disable(bus);
	DHD_BUS_UNLOCK(bus->bus_lock, flags_bus);

	if (!bus->is_linkdown) {
		uint32 status;
		status = dhdpcie_bus_cfg_read_dword(bus, PCIIntstatus, 4);
		dhdpcie_bus_cfg_write_dword(bus, PCIIntstatus, 4, status);
	}

	if (!dhd_download_fw_on_driverload) {
		dhd_dpc_kill(bus->dhd);
	}

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	pm_runtime_disable(dhd_bus_to_dev(bus));
	pm_runtime_set_suspended(dhd_bus_to_dev(bus));
	pm_runtime_enable(dhd_bus_to_dev(bus));
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

	/* Clear rx control and wake any waiters */
	dhd_os_set_ioctl_resp_timeout(IOCTL_DISABLE_TIMEOUT);
	dhd_wakeup_ioctl_event(bus->dhd, IOCTL_RETURN_ON_BUS_STOP);

done:
	return;
}

/**
 * Watchdog timer function.
 * @param dhd   Represents a specific hardware (dongle) instance that this DHD manages
 */
bool dhd_bus_watchdog(dhd_pub_t *dhd)
{
	unsigned long flags;
	dhd_bus_t *bus = dhd->bus;

	DHD_GENERAL_LOCK(dhd, flags);
	if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(dhd) ||
			DHD_BUS_CHECK_SUSPEND_OR_SUSPEND_IN_PROGRESS(dhd)) {
		DHD_GENERAL_UNLOCK(dhd, flags);
		return FALSE;
	}
	DHD_BUS_BUSY_SET_IN_WD(dhd);
	DHD_GENERAL_UNLOCK(dhd, flags);

#ifdef DHD_PCIE_RUNTIMEPM
	dhdpcie_runtime_bus_wake(dhd, TRUE, __builtin_return_address(0));
#endif /* DHD_PCIE_RUNTIMEPM */

	/* Poll for console output periodically */
	if (dhd->busstate == DHD_BUS_DATA &&
		dhd->dhd_console_ms != 0 &&
		bus->bus_low_power_state == DHD_BUS_NO_LOW_POWER_STATE) {
		bus->console.count += dhd_watchdog_ms;
		if (bus->console.count >= dhd->dhd_console_ms) {
			bus->console.count -= dhd->dhd_console_ms;

			if (MULTIBP_ENAB(bus->sih)) {
				dhd_bus_pcie_pwr_req(bus);
			}

			/* Make sure backplane clock is on */
			if (dhdpcie_bus_readconsole(bus) < 0) {
				dhd->dhd_console_ms = 0; /* On error, stop trying */
			}

			if (MULTIBP_ENAB(bus->sih)) {
				dhd_bus_pcie_pwr_req_clear(bus);
			}
		}
	}

	DHD_GENERAL_LOCK(dhd, flags);
	DHD_BUS_BUSY_CLEAR_IN_WD(dhd);
	dhd_os_busbusy_wake(dhd);
	DHD_GENERAL_UNLOCK(dhd, flags);

	return TRUE;
} /* dhd_bus_watchdog */

#if defined(SUPPORT_MULTIPLE_REVISION)
static int concate_revision_bcm4358(dhd_bus_t *bus, char *fw_path, char *nv_path)
{
	uint32 chiprev;
#if defined(SUPPORT_MULTIPLE_CHIPS)
	char chipver_tag[20] = "_4358";
#else
	char chipver_tag[10] = {0, };
#endif /* SUPPORT_MULTIPLE_CHIPS */

	chiprev = dhd_bus_chiprev(bus);
	if (chiprev == 0) {
		DHD_ERROR(("----- CHIP 4358 A0 -----\n"));
		strcat(chipver_tag, "_a0");
	} else if (chiprev == 1) {
		DHD_ERROR(("----- CHIP 4358 A1 -----\n"));
#if defined(SUPPORT_MULTIPLE_CHIPS) || defined(SUPPORT_MULTIPLE_MODULE_CIS)
		strcat(chipver_tag, "_a1");
#endif /* defined(SUPPORT_MULTIPLE_CHIPS) || defined(SUPPORT_MULTIPLE_MODULE_CIS) */
	} else if (chiprev == 3) {
		DHD_ERROR(("----- CHIP 4358 A3 -----\n"));
#if defined(SUPPORT_MULTIPLE_CHIPS)
		strcat(chipver_tag, "_a3");
#endif /* SUPPORT_MULTIPLE_CHIPS */
	} else {
		DHD_ERROR(("----- Unknown chip version, ver=%x -----\n", chiprev));
	}

	strcat(fw_path, chipver_tag);

#if defined(SUPPORT_MULTIPLE_MODULE_CIS) && defined(USE_CID_CHECK)
	if (chiprev == 1 || chiprev == 3) {
		int ret = dhd_check_module_b85a();
		if ((chiprev == 1) && (ret < 0)) {
			memset(chipver_tag, 0x00, sizeof(chipver_tag));
			strcat(chipver_tag, "_b85");
			strcat(chipver_tag, "_a1");
		}
	}

	DHD_ERROR(("%s: chipver_tag %s \n", __FUNCTION__, chipver_tag));
#endif /* defined(SUPPORT_MULTIPLE_MODULE_CIS) && defined(USE_CID_CHECK) */

#if defined(SUPPORT_MULTIPLE_BOARD_REV)
	if (system_rev >= 10) {
		DHD_ERROR(("----- Board Rev  [%d]-----\n", system_rev));
		strcat(chipver_tag, "_r10");
	}
#endif /* SUPPORT_MULTIPLE_BOARD_REV */
	strcat(nv_path, chipver_tag);

	return 0;
}

static int concate_revision_bcm4359(dhd_bus_t *bus, char *fw_path, char *nv_path)
{
	uint32 chip_ver;
	char chipver_tag[10] = {0, };
#if defined(SUPPORT_MULTIPLE_MODULE_CIS) && defined(USE_CID_CHECK) && \
	defined(SUPPORT_BCM4359_MIXED_MODULES)
	int module_type = -1;
#endif /* SUPPORT_MULTIPLE_MODULE_CIS && USE_CID_CHECK && SUPPORT_BCM4359_MIXED_MODULES */

	chip_ver = bus->sih->chiprev;
	if (chip_ver == 4) {
		DHD_ERROR(("----- CHIP 4359 B0 -----\n"));
		strncat(chipver_tag, "_b0", strlen("_b0"));
	} else if (chip_ver == 5) {
		DHD_ERROR(("----- CHIP 4359 B1 -----\n"));
		strncat(chipver_tag, "_b1", strlen("_b1"));
	} else if (chip_ver == 9) {
		DHD_ERROR(("----- CHIP 4359 C0 -----\n"));
		strncat(chipver_tag, "_c0", strlen("_c0"));
	} else {
		DHD_ERROR(("----- Unknown chip version, ver=%x -----\n", chip_ver));
		return -1;
	}

#if defined(SUPPORT_MULTIPLE_MODULE_CIS) && defined(USE_CID_CHECK) && \
	defined(SUPPORT_BCM4359_MIXED_MODULES)
	module_type =  dhd_check_module_b90();

	switch (module_type) {
		case BCM4359_MODULE_TYPE_B90B:
			strcat(fw_path, chipver_tag);
			break;
		case BCM4359_MODULE_TYPE_B90S:
		default:
			/*
			 * .cid.info file not exist case,
			 * loading B90S FW force for initial MFG boot up.
			*/
			if (chip_ver == 5) {
				strncat(fw_path, "_b90s", strlen("_b90s"));
			}
			strcat(fw_path, chipver_tag);
			strcat(nv_path, chipver_tag);
			break;
	}
#else /* SUPPORT_MULTIPLE_MODULE_CIS && USE_CID_CHECK && SUPPORT_BCM4359_MIXED_MODULES */
	strcat(fw_path, chipver_tag);
	strcat(nv_path, chipver_tag);
#endif /* SUPPORT_MULTIPLE_MODULE_CIS && USE_CID_CHECK && SUPPORT_BCM4359_MIXED_MODULES */

	return 0;
}

#if defined(USE_CID_CHECK)

#define MAX_EXTENSION 20
#define MODULE_BCM4361_INDEX	3
#define CHIP_REV_A0	1
#define CHIP_REV_A1	2
#define CHIP_REV_B0	3
#define CHIP_REV_B1	4
#define CHIP_REV_B2	5
#define CHIP_REV_C0	6
#define BOARD_TYPE_EPA				0x080f
#define BOARD_TYPE_IPA				0x0827
#define BOARD_TYPE_IPA_OLD			0x081a
#define DEFAULT_CIDINFO_FOR_EPA		"r00a_e000_a0_ePA"
#define DEFAULT_CIDINFO_FOR_IPA		"r00a_e000_a0_iPA"
#define DEFAULT_CIDINFO_FOR_A1		"r01a_e30a_a1"
#define DEFAULT_CIDINFO_FOR_B0		"r01i_e32_b0"
#define MAX_VID_LEN					8
#define CIS_TUPLE_HDR_LEN		2
#if defined(BCM4361_CHIP)
#define CIS_TUPLE_START_ADDRESS		0x18011110
#define CIS_TUPLE_END_ADDRESS		0x18011167
#elif defined(BCM4375_CHIP)
#define CIS_TUPLE_START_ADDRESS		0x18011120
#define CIS_TUPLE_END_ADDRESS		0x18011177
#endif /* defined(BCM4361_CHIP) */
#define CIS_TUPLE_MAX_COUNT		(uint32)((CIS_TUPLE_END_ADDRESS - CIS_TUPLE_START_ADDRESS\
						+ 1) / sizeof(uint32))
#define CIS_TUPLE_TAG_START			0x80
#define CIS_TUPLE_TAG_VENDOR		0x81
#define CIS_TUPLE_TAG_BOARDTYPE		0x1b
#define CIS_TUPLE_TAG_LENGTH		1
#define NVRAM_FEM_MURATA			"_murata"
#define CID_FEM_MURATA				"_mur_"

typedef struct cis_tuple_format {
	uint8	id;
	uint8	len;	/* total length of tag and data */
	uint8	tag;
	uint8	data[1];
} cis_tuple_format_t;

typedef struct {
	char cid_ext[MAX_EXTENSION];
	char nvram_ext[MAX_EXTENSION];
	char fw_ext[MAX_EXTENSION];
} naming_info_t;

naming_info_t bcm4361_naming_table[] = {
	{ {""}, {""}, {""} },
	{ {"r00a_e000_a0_ePA"}, {"_a0_ePA"}, {"_a0_ePA"} },
	{ {"r00a_e000_a0_iPA"}, {"_a0"}, {"_a1"} },
	{ {"r01a_e30a_a1"}, {"_r01a_a1"}, {"_a1"} },
	{ {"r02a_e30a_a1"}, {"_r02a_a1"}, {"_a1"} },
	{ {"r02c_e30a_a1"}, {"_r02c_a1"}, {"_a1"} },
	{ {"r01d_e31_b0"}, {"_r01d_b0"}, {"_b0"} },
	{ {"r01f_e31_b0"}, {"_r01f_b0"}, {"_b0"} },
	{ {"r02g_e31_b0"}, {"_r02g_b0"}, {"_b0"} },
	{ {"r01h_e32_b0"}, {"_r01h_b0"}, {"_b0"} },
	{ {"r01i_e32_b0"}, {"_r01i_b0"}, {"_b0"} },
	{ {"r02j_e32_b0"}, {"_r02j_b0"}, {"_b0"} },
	{ {"r012_1kl_a1"}, {"_r012_a1"}, {"_a1"} },
	{ {"r013_1kl_b0"}, {"_r013_b0"}, {"_b0"} },
	{ {"r013_1kl_b0"}, {"_r013_b0"}, {"_b0"} },
	{ {"r014_1kl_b0"}, {"_r014_b0"}, {"_b0"} },
	{ {"r015_1kl_b0"}, {"_r015_b0"}, {"_b0"} },
	{ {"r020_1kl_b0"}, {"_r020_b0"}, {"_b0"} },
	{ {"r021_1kl_b0"}, {"_r021_b0"}, {"_b0"} },
	{ {"r022_1kl_b0"}, {"_r022_b0"}, {"_b0"} },
	{ {"r023_1kl_b0"}, {"_r023_b0"}, {"_b0"} },
	{ {"r024_1kl_b0"}, {"_r024_b0"}, {"_b0"} },
	{ {"r030_1kl_b0"}, {"_r030_b0"}, {"_b0"} },
	{ {"r031_1kl_b0"}, {"_r030_b0"}, {"_b0"} },	/* exceptional case : r31 -> r30 */
	{ {"r032_1kl_b0"}, {"_r032_b0"}, {"_b0"} },
	{ {"r033_1kl_b0"}, {"_r033_b0"}, {"_b0"} },
	{ {"r034_1kl_b0"}, {"_r034_b0"}, {"_b0"} },
	{ {"r02a_e32a_b2"}, {"_r02a_b2"}, {"_b2"} },
	{ {"r02b_e32a_b2"}, {"_r02b_b2"}, {"_b2"} },
	{ {"r020_1qw_b2"}, {"_r020_b2"}, {"_b2"} },
	{ {"r021_1qw_b2"}, {"_r021_b2"}, {"_b2"} },
	{ {"r022_1qw_b2"}, {"_r022_b2"}, {"_b2"} },
	{ {"r031_1qw_b2"}, {"_r031_b2"}, {"_b2"} },
	{ {"r032_1qw_b2"}, {"_r032_b2"}, {"_b2"} },
	{ {"r041_1qw_b2"}, {"_r041_b2"}, {"_b2"} }
};

#define MODULE_BCM4375_INDEX	3

naming_info_t bcm4375_naming_table[] = {
	{ {""}, {""}, {""} },
	{ {"e41_es11"}, {"_ES00_semco_b0"}, {"_b0"} },
	{ {"e43_es33"}, {"_ES01_semco_b0"}, {"_b0"} },
	{ {"e43_es34"}, {"_ES02_semco_b0"}, {"_b0"} },
	{ {"e43_es35"}, {"_ES02_semco_b0"}, {"_b0"} },
	{ {"e43_es36"}, {"_ES03_semco_b0"}, {"_b0"} },
	{ {"e43_cs41"}, {"_CS00_semco_b1"}, {"_b1"} },
	{ {"e43_cs51"}, {"_CS01_semco_b1"}, {"_b1"} },
	{ {"e43_cs53"}, {"_CS01_semco_b1"}, {"_b1"} },
	{ {"e43_cs61"}, {"_CS00_skyworks_b1"}, {"_b1"} },
	{ {"1rh_es10"}, {"_1rh_es10_b0"}, {"_b0"} },
	{ {"1rh_es11"}, {"_1rh_es11_b0"}, {"_b0"} },
	{ {"1rh_es12"}, {"_1rh_es12_b0"}, {"_b0"} },
	{ {"1rh_es13"}, {"_1rh_es13_b0"}, {"_b0"} },
	{ {"1rh_es20"}, {"_1rh_es20_b0"}, {"_b0"} },
	{ {"1rh_es32"}, {"_1rh_es32_b0"}, {"_b0"} },
	{ {"1rh_es41"}, {"_1rh_es41_b1"}, {"_b1"} },
	{ {"1rh_es42"}, {"_1rh_es42_b1"}, {"_b1"} },
	{ {"1rh_es43"}, {"_1rh_es43_b1"}, {"_b1"} },
	{ {"1rh_es44"}, {"_1rh_es44_b1"}, {"_b1"} }
};

static naming_info_t *
dhd_find_naming_info(naming_info_t table[], int table_size, char *module_type)
{
	int index_found = 0, i = 0;

	if (module_type && strlen(module_type) > 0) {
		for (i = 1; i < table_size; i++) {
			if (!strncmp(table[i].cid_ext, module_type, strlen(table[i].cid_ext))) {
				index_found = i;
				break;
			}
		}
	}

	DHD_INFO(("%s: index_found=%d\n", __FUNCTION__, index_found));

	return &table[index_found];
}

static naming_info_t *
dhd_find_naming_info_by_cid(naming_info_t table[], int table_size,
	char *cid_info)
{
	int index_found = 0, i = 0;
	char *ptr;

	/* truncate extension */
	for (i = 1, ptr = cid_info; i < MODULE_BCM4361_INDEX && ptr; i++) {
		ptr = bcmstrstr(ptr, "_");
		if (ptr) {
			ptr++;
		}
	}

	for (i = 1; i < table_size && ptr; i++) {
		if (!strncmp(table[i].cid_ext, ptr, strlen(table[i].cid_ext))) {
			index_found = i;
			break;
		}
	}

	DHD_INFO(("%s: index_found=%d\n", __FUNCTION__, index_found));

	return &table[index_found];
}

static int
dhd_parse_board_information_bcm(dhd_bus_t *bus, int *boardtype,
	unsigned char *vid, int *vid_length)
{
	int boardtype_backplane_addr[] = {
		0x18010324, /* OTP Control 1 */
		0x18012618, /* PMU min resource mask */
	};
	int boardtype_backplane_data[] = {
		0x00fa0000,
		0x0e4fffff /* Keep on ARMHTAVAIL */
	};
	int int_val = 0, i = 0;
	cis_tuple_format_t *tuple;
	int totlen, len;
	uint32 raw_data[CIS_TUPLE_MAX_COUNT];

	for (i = 0; i < ARRAYSIZE(boardtype_backplane_addr); i++) {
		/* Write new OTP and PMU configuration */
		if (si_backplane_access(bus->sih, boardtype_backplane_addr[i], sizeof(int),
				&boardtype_backplane_data[i], FALSE) != BCME_OK) {
			DHD_ERROR(("invalid size/addr combination\n"));
			return BCME_ERROR;
		}

		if (si_backplane_access(bus->sih, boardtype_backplane_addr[i], sizeof(int),
				&int_val, TRUE) != BCME_OK) {
			DHD_ERROR(("invalid size/addr combination\n"));
			return BCME_ERROR;
		}

		DHD_INFO(("%s: boardtype_backplane_addr 0x%08x rdata 0x%04x\n",
			__FUNCTION__, boardtype_backplane_addr[i], int_val));
	}

	/* read tuple raw data */
	for (i = 0; i < CIS_TUPLE_MAX_COUNT; i++) {
		if (si_backplane_access(bus->sih, CIS_TUPLE_START_ADDRESS + i * sizeof(uint32),
				sizeof(uint32),	&raw_data[i], TRUE) != BCME_OK) {
			break;
		}
	}

	totlen = i * sizeof(uint32);
	tuple = (cis_tuple_format_t *)raw_data;

	/* check the first tuple has tag 'start' */
	if (tuple->id != CIS_TUPLE_TAG_START) {
		return BCME_ERROR;
	}

	*vid_length = *boardtype = 0;

	/* find tagged parameter */
	while ((totlen >= (tuple->len + CIS_TUPLE_HDR_LEN)) &&
			(*vid_length == 0 || *boardtype == 0)) {
		len = tuple->len;

		if ((tuple->tag == CIS_TUPLE_TAG_VENDOR) &&
				(totlen >= (int)(len + CIS_TUPLE_HDR_LEN))) {
			/* found VID */
			memcpy(vid, tuple->data, tuple->len - CIS_TUPLE_TAG_LENGTH);
			*vid_length = tuple->len - CIS_TUPLE_TAG_LENGTH;
			prhex("OTP VID", tuple->data, tuple->len - CIS_TUPLE_TAG_LENGTH);
		}
		else if ((tuple->tag == CIS_TUPLE_TAG_BOARDTYPE) &&
				(totlen >= (int)(len + CIS_TUPLE_HDR_LEN))) {
			/* found boardtype */
			*boardtype = (int)tuple->data[0];
			prhex("OTP boardtype", tuple->data, tuple->len - CIS_TUPLE_TAG_LENGTH);
		}

		tuple = (cis_tuple_format_t*)((uint8*)tuple + (len + CIS_TUPLE_HDR_LEN));
		totlen -= (len + CIS_TUPLE_HDR_LEN);
	}

	if (*vid_length <= 0 || *boardtype <= 0) {
		DHD_ERROR(("failed to parse information (vid=%d, boardtype=%d)\n",
			*vid_length, *boardtype));
		return BCME_ERROR;
	}

	return BCME_OK;

}

static naming_info_t *
dhd_find_naming_info_by_chip_rev(naming_info_t table[], int table_size,
	dhd_bus_t *bus, bool *is_murata_fem)
{
	int board_type = 0, chip_rev = 0, vid_length = 0;
	unsigned char vid[MAX_VID_LEN];
	naming_info_t *info = &table[0];
	char *cid_info = NULL;

	if (!bus || !bus->sih) {
		DHD_ERROR(("%s:bus(%p) or bus->sih is NULL\n", __FUNCTION__, bus));
		return NULL;
	}
	chip_rev = bus->sih->chiprev;

	if (dhd_parse_board_information_bcm(bus, &board_type, vid, &vid_length)
			!= BCME_OK) {
		DHD_ERROR(("%s:failed to parse board information\n", __FUNCTION__));
		return NULL;
	}

	DHD_INFO(("%s:chip version %d\n", __FUNCTION__, chip_rev));

#if defined(BCM4361_CHIP)
	/* A0 chipset has exception only */
	if (chip_rev == CHIP_REV_A0) {
		if (board_type == BOARD_TYPE_EPA) {
			info = dhd_find_naming_info(table, table_size,
				DEFAULT_CIDINFO_FOR_EPA);
		} else if ((board_type == BOARD_TYPE_IPA) ||
				(board_type == BOARD_TYPE_IPA_OLD)) {
			info = dhd_find_naming_info(table, table_size,
				DEFAULT_CIDINFO_FOR_IPA);
		}
	} else {
		cid_info = dhd_get_cid_info(vid, vid_length);
		if (cid_info) {
			info = dhd_find_naming_info_by_cid(table, table_size, cid_info);
			if (strstr(cid_info, CID_FEM_MURATA)) {
				*is_murata_fem = TRUE;
			}
		}
	}
#else
	cid_info = dhd_get_cid_info(vid, vid_length);
	if (cid_info) {
		info = dhd_find_naming_info_by_cid(table, table_size, cid_info);
		if (strstr(cid_info, CID_FEM_MURATA)) {
			*is_murata_fem = TRUE;
		}
	}
#endif /* BCM4361_CHIP */

	return info;
}
#endif /* USE_CID_CHECK */

static int
concate_revision_bcm4361(dhd_bus_t *bus, char *fw_path, char *nv_path)
{
	int ret = BCME_OK;
#if defined(SUPPORT_BCM4361_MIXED_MODULES) && defined(USE_CID_CHECK)
	char module_type[MAX_VNAME_LEN];
	naming_info_t *info = NULL;
	bool is_murata_fem = FALSE;

	memset(module_type, 0, sizeof(module_type));

	if (dhd_check_module_bcm(module_type,
			MODULE_BCM4361_INDEX, &is_murata_fem) == BCME_OK) {
		info = dhd_find_naming_info(bcm4361_naming_table,
			ARRAYSIZE(bcm4361_naming_table), module_type);
	} else {
		/* in case of .cid.info doesn't exists */
		info = dhd_find_naming_info_by_chip_rev(bcm4361_naming_table,
			ARRAYSIZE(bcm4361_naming_table), bus, &is_murata_fem);
	}

	if (bcmstrnstr(nv_path, PATH_MAX,  "_murata", 7)) {
		is_murata_fem = FALSE;
	}

	if (info) {
		if (is_murata_fem) {
			strncat(nv_path, NVRAM_FEM_MURATA, strlen(NVRAM_FEM_MURATA));
		}
		strncat(nv_path, info->nvram_ext, strlen(info->nvram_ext));
		strncat(fw_path, info->fw_ext, strlen(info->fw_ext));
	} else {
		DHD_ERROR(("%s:failed to find extension for nvram and firmware\n", __FUNCTION__));
		ret = BCME_ERROR;
	}
#else /* SUPPORT_MULTIPLE_MODULE_CIS && USE_CID_CHECK */
	char chipver_tag[10] = {0, };

	strcat(fw_path, chipver_tag);
	strcat(nv_path, chipver_tag);
#endif /* SUPPORT_MULTIPLE_MODULE_CIS && USE_CID_CHECK */

	return ret;
}

static int
concate_revision_bcm4375(dhd_bus_t *bus, char *fw_path, char *nv_path)
{
	int ret = BCME_OK;
#if defined(SUPPORT_BCM4375_MIXED_MODULES) && defined(USE_CID_CHECK)
	char module_type[MAX_VNAME_LEN];
	naming_info_t *info = NULL;
	bool is_murata_fem = FALSE;

	memset(module_type, 0, sizeof(module_type));

	if (dhd_check_module_bcm(module_type,
			MODULE_BCM4375_INDEX, &is_murata_fem) == BCME_OK) {
		info = dhd_find_naming_info(bcm4375_naming_table,
				ARRAYSIZE(bcm4375_naming_table), module_type);
	} else {
		/* in case of .cid.info doesn't exists */
		info = dhd_find_naming_info_by_chip_rev(bcm4375_naming_table,
				ARRAYSIZE(bcm4375_naming_table), bus, &is_murata_fem);
	}

	if (info) {
		strncat(nv_path, info->nvram_ext, strlen(info->nvram_ext));
		strncat(fw_path, info->fw_ext, strlen(info->fw_ext));
	} else {
		DHD_ERROR(("%s:failed to find extension for nvram and firmware\n", __FUNCTION__));
		ret = BCME_ERROR;
	}
#else /* SUPPORT_BCM4375_MIXED_MODULES && USE_CID_CHECK */
	char chipver_tag[10] = {0, };

	strcat(fw_path, chipver_tag);
	strcat(nv_path, chipver_tag);
#endif /* SUPPORT_BCM4375_MIXED_MODULES && USE_CID_CHECK */

	return ret;
}

int
concate_revision(dhd_bus_t *bus, char *fw_path, char *nv_path)
{
	int res = 0;

	if (!bus || !bus->sih) {
		DHD_ERROR(("%s:Bus is Invalid\n", __FUNCTION__));
		return -1;
	}

	if (!fw_path || !nv_path) {
		DHD_ERROR(("fw_path or nv_path is null.\n"));
		return res;
	}

	switch (si_chipid(bus->sih)) {

	case BCM43569_CHIP_ID:
	case BCM4358_CHIP_ID:
		res = concate_revision_bcm4358(bus, fw_path, nv_path);
		break;
	case BCM4355_CHIP_ID:
	case BCM4359_CHIP_ID:
		res = concate_revision_bcm4359(bus, fw_path, nv_path);
		break;
	case BCM4361_CHIP_ID:
	case BCM4347_CHIP_ID:
		res = concate_revision_bcm4361(bus, fw_path, nv_path);
		break;
	case BCM4375_CHIP_ID:
		res = concate_revision_bcm4375(bus, fw_path, nv_path);
		break;
	default:
		DHD_ERROR(("REVISION SPECIFIC feature is not required\n"));
		return res;
	}

	return res;
}
#endif /* SUPPORT_MULTIPLE_REVISION */

uint16
dhd_get_chipid(dhd_pub_t *dhd)
{
	dhd_bus_t *bus = dhd->bus;

	if (bus && bus->sih)
		return (uint16)si_chipid(bus->sih);
	else
		return 0;
}

/**
 * Loads firmware given by caller supplied path and nvram image into PCIe dongle.
 *
 * BCM_REQUEST_FW specific :
 * Given the chip type, determines the to be used file paths within /lib/firmware/brcm/ containing
 * firmware and nvm for that chip. If the download fails, retries download with a different nvm file
 *
 * BCMEMBEDIMAGE specific:
 * If bus->fw_path is empty, or if the download of bus->fw_path failed, firmware contained in header
 * file will be used instead.
 *
 * @return BCME_OK on success
 */
int
dhd_bus_download_firmware(struct dhd_bus *bus, osl_t *osh,
                          char *pfw_path, char *pnv_path)
{
	int ret;

	bus->fw_path = pfw_path;
	bus->nv_path = pnv_path;

#if defined(SUPPORT_MULTIPLE_REVISION)
	if (concate_revision(bus, bus->fw_path, bus->nv_path) != 0) {
		DHD_ERROR(("%s: fail to concatnate revison \n",
			__FUNCTION__));
		return BCME_BADARG;
	}
#endif /* SUPPORT_MULTIPLE_REVISION */

#if defined(DHD_BLOB_EXISTENCE_CHECK)
	dhd_set_blob_support(bus->dhd, bus->fw_path);
#endif /* DHD_BLOB_EXISTENCE_CHECK */

	DHD_ERROR(("%s: firmware path=%s, nvram path=%s\n",
		__FUNCTION__, bus->fw_path, bus->nv_path));
	dhdpcie_dump_resource(bus);

	ret = dhdpcie_download_firmware(bus, osh);

	return ret;
}

/**
 * Loads firmware given by 'bus->fw_path' into PCIe dongle.
 *
 * BCM_REQUEST_FW specific :
 * Given the chip type, determines the to be used file paths within /lib/firmware/brcm/ containing
 * firmware and nvm for that chip. If the download fails, retries download with a different nvm file
 *
 * BCMEMBEDIMAGE specific:
 * If bus->fw_path is empty, or if the download of bus->fw_path failed, firmware contained in header
 * file will be used instead.
 *
 * @return BCME_OK on success
 */
static int
dhdpcie_download_firmware(struct dhd_bus *bus, osl_t *osh)
{
	int ret = 0;
#if defined(BCM_REQUEST_FW)
	uint chipid = bus->sih->chip;
	uint revid = bus->sih->chiprev;
	char fw_path[64] = "/lib/firmware/brcm/bcm";	/* path to firmware image */
	char nv_path[64];		/* path to nvram vars file */
	bus->fw_path = fw_path;
	bus->nv_path = nv_path;
	switch (chipid) {
	case BCM43570_CHIP_ID:
		bcmstrncat(fw_path, "43570", 5);
		switch (revid) {
		case 0:
			bcmstrncat(fw_path, "a0", 2);
			break;
		case 2:
			bcmstrncat(fw_path, "a2", 2);
			break;
		default:
			DHD_ERROR(("%s: revid is not found %x\n", __FUNCTION__,
			revid));
			break;
		}
		break;
	default:
		DHD_ERROR(("%s: unsupported device %x\n", __FUNCTION__,
		chipid));
		return 0;
	}
	/* load board specific nvram file */
	snprintf(bus->nv_path, sizeof(nv_path), "%s.nvm", fw_path);
	/* load firmware */
	snprintf(bus->fw_path, sizeof(fw_path), "%s-firmware.bin", fw_path);
#endif /* BCM_REQUEST_FW */

	DHD_OS_WAKE_LOCK(bus->dhd);
	ret = _dhdpcie_download_firmware(bus);

	DHD_OS_WAKE_UNLOCK(bus->dhd);
	return ret;
} /* dhdpcie_download_firmware */

#define DHD_MEMORY_SET_PATTERN 0xAA

/**
 * Downloads a file containing firmware into dongle memory. In case of a .bea file, the DHD
 * is updated with the event logging partitions within that file as well.
 *
 * @param pfw_path    Path to .bin or .bea file
 */
static int
dhdpcie_download_code_file(struct dhd_bus *bus, char *pfw_path)
{
	int bcmerror = BCME_ERROR;
	int offset = 0;
	int len = 0;
	bool store_reset;
	char *imgbuf = NULL;
	uint8 *memblock = NULL, *memptr = NULL;
	int offset_end = bus->ramsize;
	uint32 file_size = 0, read_len = 0;
	struct trx_header *trx_hdr;
	bool trx_chk = TRUE;

#if defined(DHD_FW_MEM_CORRUPTION)
	if (dhd_bus_get_fw_mode(bus->dhd) == DHD_FLAG_MFG_MODE) {
		dhd_tcm_test_enable = TRUE;
	} else {
		dhd_tcm_test_enable = FALSE;
	}
#endif /* DHD_FW_MEM_CORRUPTION */
	DHD_ERROR(("%s: dhd_tcm_test_enable %u\n", __FUNCTION__, dhd_tcm_test_enable));
	/* TCM check */
	if (dhd_tcm_test_enable && !dhd_bus_tcm_test(bus)) {
		DHD_ERROR(("dhd_bus_tcm_test failed\n"));
		bcmerror = BCME_ERROR;
		goto err;
	}
	DHD_ERROR(("%s: download firmware %s\n", __FUNCTION__, pfw_path));

	/* Should succeed in opening image if it is actually given through registry
	 * entry or in module param.
	 */
	imgbuf = dhd_os_open_image1(bus->dhd, pfw_path);
	if (imgbuf == NULL) {
		goto err;
	}

	file_size = dhd_os_get_image_size(imgbuf);
	if (!file_size) {
		DHD_ERROR(("%s: get file size fails ! \n", __FUNCTION__));
		goto err;
	}

	memptr = memblock = MALLOC(bus->dhd->osh, MEMBLOCK + DHD_SDALIGN);
	if (memblock == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory %d bytes\n", __FUNCTION__, MEMBLOCK));
		bcmerror = BCME_NOMEM;
		goto err;
	}
	if ((uint32)(uintptr)memblock % DHD_SDALIGN) {
		memptr += (DHD_SDALIGN - ((uint32)(uintptr)memblock % DHD_SDALIGN));
	}

	/* check if CR4/CA7 */
	store_reset = (si_setcore(bus->sih, ARMCR4_CORE_ID, 0) ||
			si_setcore(bus->sih, ARMCA7_CORE_ID, 0));
	/* Download image with MEMBLOCK size */
	while ((len = dhd_os_get_image_block((char*)memptr, MEMBLOCK, imgbuf))) {
		if (len < 0) {
			DHD_ERROR(("%s: dhd_os_get_image_block failed (%d)\n", __FUNCTION__, len));
			bcmerror = BCME_ERROR;
			goto err;
		}
			read_len += len;
		if (read_len > file_size) {
			DHD_ERROR(("%s: WARNING! reading beyond EOF, len=%d; read_len=%u;"
				" file_size=%u truncating len to %d \n", __FUNCTION__,
				len, read_len, file_size, (len - (read_len - file_size))));
			len -= (read_len - file_size);
		}

		/* if address is 0, store the reset instruction to be written in 0 */
		if (store_reset) {
			ASSERT(offset == 0);
			bus->resetinstr = *(((uint32*)memptr));
			/* Add start of RAM address to the address given by user */
			offset += bus->dongle_ram_base;
			offset_end += offset;
			store_reset = FALSE;
		}

		/* Check for trx file */
		if (trx_chk && (len >= sizeof(struct trx_header))) {
			trx_chk = FALSE;
			trx_hdr = (struct trx_header *)memptr;
			if (trx_hdr->magic == TRX_MAGIC) {
				/* CYW55560, we need to write TRX header at RAMSTART */
				offset -= sizeof(struct trx_header);
			}
		}

		bcmerror = dhdpcie_bus_membytes(bus, TRUE, offset, (uint8 *)memptr, len);
		if (bcmerror) {
			DHD_ERROR(("%s: error %d on writing %d membytes at 0x%08x\n",
				__FUNCTION__, bcmerror, MEMBLOCK, offset));
			goto err;
		}
		offset += MEMBLOCK;

		if (offset >= offset_end) {
			DHD_ERROR(("%s: invalid address access to %x (offset end: %x)\n",
				__FUNCTION__, offset, offset_end));
			bcmerror = BCME_ERROR;
			goto err;
		}

		if (read_len >= file_size) {
			break;
		}
	}
err:
	if (memblock) {
		MFREE(bus->dhd->osh, memblock, MEMBLOCK + DHD_SDALIGN);
	}

	if (imgbuf) {
		dhd_os_close_image1(bus->dhd, imgbuf);
	}

	return bcmerror;
} /* dhdpcie_download_code_file */

#ifdef CUSTOMER_HW4_DEBUG
#define MIN_NVRAMVARS_SIZE 128
#endif /* CUSTOMER_HW4_DEBUG */

static int
dhdpcie_download_nvram(struct dhd_bus *bus)
{
	int bcmerror = BCME_ERROR;
	uint len;
	char * memblock = NULL;
	char *bufp;
	char *pnv_path;
	bool nvram_file_exists;
	bool nvram_uefi_exists = FALSE;
	bool local_alloc = FALSE;
	pnv_path = bus->nv_path;

	nvram_file_exists = ((pnv_path != NULL) && (pnv_path[0] != '\0'));

	/* First try UEFI */
	len = MAX_NVRAMBUF_SIZE;
	dhd_get_download_buffer(bus->dhd, NULL, NVRAM, &memblock, (int *)&len);

	/* If UEFI empty, then read from file system */
	if ((len <= 0) || (memblock == NULL)) {

		if (nvram_file_exists) {
			len = MAX_NVRAMBUF_SIZE;
			dhd_get_download_buffer(bus->dhd, pnv_path, NVRAM, &memblock, (int *)&len);
			if ((len <= 0 || len > MAX_NVRAMBUF_SIZE)) {
				goto err;
			}
		}
		else {
			/* For SROM OTP no external file or UEFI required */
			bcmerror = BCME_OK;
		}
	} else {
		nvram_uefi_exists = TRUE;
	}

	DHD_ERROR(("%s: dhd_get_download_buffer len %d\n", __FUNCTION__, len));

	if (len > 0 && len <= MAX_NVRAMBUF_SIZE && memblock != NULL) {
		bufp = (char *) memblock;

		{
			bufp[len] = 0;
			if (nvram_uefi_exists || nvram_file_exists) {
				len = process_nvram_vars(bufp, len);
			}
		}

		DHD_ERROR(("%s: process_nvram_vars len %d\n", __FUNCTION__, len));
#ifdef CUSTOMER_HW4_DEBUG
		if (len < MIN_NVRAMVARS_SIZE) {
			DHD_ERROR(("%s: invalid nvram size in process_nvram_vars \n",
				__FUNCTION__));
			bcmerror = BCME_ERROR;
			goto err;
		}
#endif /* CUSTOMER_HW4_DEBUG */

		if (len % 4) {
			len += 4 - (len % 4);
		}
		bufp += len;
		*bufp++ = 0;
		if (len)
			bcmerror = dhdpcie_downloadvars(bus, memblock, len + 1);
		if (bcmerror) {
			DHD_ERROR(("%s: error downloading vars: %d\n",
				__FUNCTION__, bcmerror));
		}
	}

err:
	if (memblock) {
		if (local_alloc) {
			MFREE(bus->dhd->osh, memblock, MAX_NVRAMBUF_SIZE);
		} else {
			dhd_free_download_buffer(bus->dhd, memblock, MAX_NVRAMBUF_SIZE);
		}
	}

	return bcmerror;
}

static int
dhdpcie_ramsize_read_image(struct dhd_bus *bus, char *buf, int len)
{
	int bcmerror = BCME_ERROR;
	char *imgbuf = NULL;

	if (buf == NULL || len == 0)
		goto err;

	/* External image takes precedence if specified */
	if ((bus->fw_path != NULL) && (bus->fw_path[0] != '\0')) {
		// opens and seeks to correct file offset:
		imgbuf = dhd_os_open_image1(bus->dhd, bus->fw_path);
		if (imgbuf == NULL) {
			DHD_ERROR(("%s: Failed to open firmware file\n", __FUNCTION__));
			goto err;
		}

		/* Read it */
		if (len != dhd_os_get_image_block(buf, len, imgbuf)) {
			DHD_ERROR(("%s: Failed to read %d bytes data\n", __FUNCTION__, len));
			goto err;
		}

		bcmerror = BCME_OK;
	}

err:
	if (imgbuf)
		dhd_os_close_image1(bus->dhd, imgbuf);

	return bcmerror;
}

/* The ramsize can be changed in the dongle image, for example 4365 chip share the sysmem
 * with BMC and we can adjust how many sysmem belong to CA7 during dongle compilation.
 * So in DHD we need to detect this case and update the correct dongle RAMSIZE as well.
 */
static void
dhdpcie_ramsize_adj(struct dhd_bus *bus)
{
	int i, search_len = 0;
	uint8 *memptr = NULL;
	uint8 *ramsizeptr = NULL;
	uint ramsizelen;
	uint32 ramsize_ptr_ptr[] = {RAMSIZE_PTR_PTR_LIST};
	hnd_ramsize_ptr_t ramsize_info;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	/* Adjust dongle RAMSIZE already called. */
	if (bus->ramsize_adjusted) {
		return;
	}

	/* success or failure,  we don't want to be here
	 * more than once.
	 */
	bus->ramsize_adjusted = TRUE;

	/* Not handle if user restrict dongle ram size enabled */
	if (dhd_dongle_memsize) {
		DHD_ERROR(("%s: user restrict dongle ram size to %d.\n", __FUNCTION__,
			dhd_dongle_memsize));
		return;
	}

	/* Out immediately if no image to download */
	if ((bus->fw_path == NULL) || (bus->fw_path[0] == '\0')) {
		DHD_ERROR(("%s: no fimrware file\n", __FUNCTION__));
		return;
	}

	/* Get maximum RAMSIZE info search length */
	for (i = 0; ; i++) {
		if (ramsize_ptr_ptr[i] == RAMSIZE_PTR_PTR_END)
			break;

		if (search_len < (int)ramsize_ptr_ptr[i])
			search_len = (int)ramsize_ptr_ptr[i];
	}

	if (!search_len)
		return;

	search_len += sizeof(hnd_ramsize_ptr_t);

	memptr = MALLOC(bus->dhd->osh, search_len);
	if (memptr == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory %d bytes\n", __FUNCTION__, search_len));
		return;
	}

	/* External image takes precedence if specified */
	if (dhdpcie_ramsize_read_image(bus, (char *)memptr, search_len) != BCME_OK) {
		goto err;
	}
	else {
		ramsizeptr = memptr;
		ramsizelen = search_len;
	}

	if (ramsizeptr) {
		/* Check Magic */
		for (i = 0; ; i++) {
			if (ramsize_ptr_ptr[i] == RAMSIZE_PTR_PTR_END)
				break;

			if (ramsize_ptr_ptr[i] + sizeof(hnd_ramsize_ptr_t) > ramsizelen)
				continue;

			memcpy((char *)&ramsize_info, ramsizeptr + ramsize_ptr_ptr[i],
				sizeof(hnd_ramsize_ptr_t));

			if (ramsize_info.magic == HTOL32(HND_RAMSIZE_PTR_MAGIC)) {
				bus->orig_ramsize = LTOH32(ramsize_info.ram_size);
				bus->ramsize = LTOH32(ramsize_info.ram_size);
				DHD_ERROR(("%s: Adjust dongle RAMSIZE to 0x%x\n", __FUNCTION__,
					bus->ramsize));
				break;
			}
		}
	}

err:
	if (memptr)
		MFREE(bus->dhd->osh, memptr, search_len);

	return;
} /* dhdpcie_ramsize_adj */

/**
 * Downloads firmware file given by 'bus->fw_path' into PCIe dongle
 *
 * BCMEMBEDIMAGE specific:
 * If bus->fw_path is empty, or if the download of bus->fw_path failed, firmware contained in header
 * file will be used instead.
 *
 */
static int
_dhdpcie_download_firmware(struct dhd_bus *bus)
{
	int bcmerror = -1;

	bool embed = FALSE;	/* download embedded firmware */
	bool dlok = FALSE;	/* download firmware succeeded */

	/* Out immediately if no image to download */
	if ((bus->fw_path == NULL) || (bus->fw_path[0] == '\0')) {
		DHD_ERROR(("%s: no fimrware file\n", __FUNCTION__));
		return 0;
	}
	/* Adjust ram size */
	dhdpcie_ramsize_adj(bus);

	/* Keep arm in reset */
	if (dhdpcie_bus_download_state(bus, TRUE)) {
		DHD_ERROR(("%s: error placing ARM core in reset\n", __FUNCTION__));
		goto err;
	}

	/* External image takes precedence if specified */
	if ((bus->fw_path != NULL) && (bus->fw_path[0] != '\0')) {
		if (dhdpcie_download_code_file(bus, bus->fw_path)) {
			DHD_ERROR(("%s:%d dongle image file download failed\n", __FUNCTION__,
				__LINE__));
			goto err;
		} else {
			embed = FALSE;
			dlok = TRUE;
		}
	}

	BCM_REFERENCE(embed);
	if (!dlok) {
		DHD_ERROR(("%s:%d dongle image download failed\n", __FUNCTION__, __LINE__));
		goto err;
	}

	/* EXAMPLE: nvram_array */
	/* If a valid nvram_arry is specified as above, it can be passed down to dongle */
	/* dhd_bus_set_nvram_params(bus, (char *)&nvram_array); */

	/* External nvram takes precedence if specified */
	if (dhdpcie_download_nvram(bus)) {
		DHD_ERROR(("%s:%d dongle nvram file download failed\n", __FUNCTION__, __LINE__));
		goto err;
	}

	/* Take arm out of reset */
	if (dhdpcie_bus_download_state(bus, FALSE)) {
		DHD_ERROR(("%s: error getting out of ARM core reset\n", __FUNCTION__));
		goto err;
	}

	bcmerror = 0;

err:
	return bcmerror;
} /* _dhdpcie_download_firmware */

static int
dhdpcie_bus_readconsole(dhd_bus_t *bus)
{
	dhd_console_t *c = &bus->console;
	uint8 line[CONSOLE_LINE_MAX], ch;
	uint32 n, idx, addr;
	int rv;
	uint readlen = 0;
	uint i = 0;

	if (!DHD_FWLOG_ON())
		return 0;

	/* Don't do anything until FWREADY updates console address */
	if (bus->console_addr == 0)
		return -1;

	/* Read console log struct */
	addr = bus->console_addr + OFFSETOF(hnd_cons_t, log);

	/* Check if console log struct addr has changed */
	/* Save the address(Local copy) */
	if (c->log_addr != addr) {
		/* Reset last index pointer */
		c->last = 0;
		/* Re-allocate memory if console address changes */
		if (c->buf) {
			MFREE(bus->dhd->osh, c->buf, c->bufsize);
			c->buf = NULL;
		}
		/* Save new console address */
		c->log_addr = addr;
	}

	if ((rv = dhdpcie_bus_membytes(bus, FALSE, addr, (uint8 *)&c->log, sizeof(c->log))) < 0)
		return rv;

	/* Allocate console buffer (one time only) */
	if (c->buf == NULL) {
		c->bufsize = ltoh32(c->log.buf_size);
		if ((c->buf = MALLOC(bus->dhd->osh, c->bufsize)) == NULL)
			return BCME_NOMEM;
		DHD_ERROR(("conlog: bufsize=0x%x\n", c->bufsize));
	}
	idx = ltoh32(c->log.idx);

	/* Protect against corrupt value */
	if (idx > c->bufsize)
		return BCME_ERROR;

	/* Skip reading the console buffer if the index pointer has not moved */
	if (idx == c->last)
		return BCME_OK;

	DHD_ERROR(("conlog: addr=0x%x, idx=0x%x, last=0x%x \n", c->log.buf,
	   idx, c->last));

	/* Read the console buffer data to a local buffer */
	/* optimize and read only the portion of the buffer needed, but
	 * important to handle wrap-around.
	 */
	addr = ltoh32(c->log.buf);

	/* wrap around case - write ptr < read ptr */
	if (idx < c->last) {
		/* from read ptr to end of buffer */
		readlen = c->bufsize - c->last;
		if ((rv = dhdpcie_bus_membytes(bus, FALSE,
				addr + c->last, c->buf, readlen)) < 0) {
			DHD_ERROR(("conlog: read error[1] ! \n"));
			return rv;
		}
		/* from beginning of buffer to write ptr */
		if ((rv = dhdpcie_bus_membytes(bus, FALSE,
				addr, c->buf + readlen,
				idx)) < 0) {
			DHD_ERROR(("conlog: read error[2] ! \n"));
			return rv;
		}
		readlen += idx;
	} else {
		/* non-wraparound case, write ptr > read ptr */
		readlen = (uint)idx - c->last;
		if ((rv = dhdpcie_bus_membytes(bus, FALSE,
				addr + c->last, c->buf, readlen)) < 0) {
			DHD_ERROR(("conlog: read error[3] ! \n"));
			return rv;
		}
	}
	/* update read ptr */
	c->last = idx;

	/* now output the read data from the local buffer to the host console */
	while (i < readlen) {
		for (n = 0; n < CONSOLE_LINE_MAX - 2 && i < readlen; n++) {
			ch = c->buf[i];
			++i;
			if (ch == '\n')
				break;
			line[n] = ch;
		}

		if (n > 0) {
			if (line[n - 1] == '\r')
				n--;
			line[n] = 0;
			DHD_FWLOG(("CONSOLE: %s\n", line));
		}
	}

	return BCME_OK;

} /* dhdpcie_bus_readconsole */

void
dhd_bus_dump_console_buffer(dhd_bus_t *bus)
{
	uint32 n, i;
	uint32 addr;
	char *console_buffer = NULL;
	uint32 console_ptr, console_size, console_index;
	uint8 line[CONSOLE_LINE_MAX], ch;
	int rv;

	DHD_ERROR(("%s: Dump Complete Console Buffer\n", __FUNCTION__));

	if (bus->is_linkdown) {
		DHD_ERROR(("%s: Skip dump Console Buffer due to PCIe link down\n", __FUNCTION__));
		return;
	}

	addr =	bus->pcie_sh->console_addr + OFFSETOF(hnd_cons_t, log);
	if ((rv = dhdpcie_bus_membytes(bus, FALSE, addr,
		(uint8 *)&console_ptr, sizeof(console_ptr))) < 0) {
		goto exit;
	}

	addr =	bus->pcie_sh->console_addr + OFFSETOF(hnd_cons_t, log.buf_size);
	if ((rv = dhdpcie_bus_membytes(bus, FALSE, addr,
		(uint8 *)&console_size, sizeof(console_size))) < 0) {
		goto exit;
	}

	addr =	bus->pcie_sh->console_addr + OFFSETOF(hnd_cons_t, log.idx);
	if ((rv = dhdpcie_bus_membytes(bus, FALSE, addr,
		(uint8 *)&console_index, sizeof(console_index))) < 0) {
		goto exit;
	}

	console_ptr = ltoh32(console_ptr);
	console_size = ltoh32(console_size);
	console_index = ltoh32(console_index);

	if (console_size > CONSOLE_BUFFER_MAX ||
		!(console_buffer = MALLOC(bus->dhd->osh, console_size))) {
		goto exit;
	}

	if ((rv = dhdpcie_bus_membytes(bus, FALSE, console_ptr,
		(uint8 *)console_buffer, console_size)) < 0) {
		goto exit;
	}

	for (i = 0, n = 0; i < console_size; i += n + 1) {
		for (n = 0; n < CONSOLE_LINE_MAX - 2; n++) {
			ch = console_buffer[(console_index + i + n) % console_size];
			if (ch == '\n')
				break;
			line[n] = ch;
		}

		if (n > 0) {
			if (line[n - 1] == '\r')
				n--;
			line[n] = 0;
			/* Don't use DHD_ERROR macro since we print
			 * a lot of information quickly. The macro
			 * will truncate a lot of the printfs
			 */

			DHD_FWLOG(("CONSOLE: %s\n", line));
		}
	}

exit:
	if (console_buffer)
		MFREE(bus->dhd->osh, console_buffer, console_size);
	return;
}

/**
 * Opens the file given by bus->fw_path, reads part of the file into a buffer and closes the file.
 *
 * @return BCME_OK on success
 */
static int
dhdpcie_checkdied(dhd_bus_t *bus, char *data, uint size)
{
	int bcmerror = 0;
	uint msize = 512;
	char *mbuffer = NULL;
	uint maxstrlen = 256;
	char *str = NULL;
	pciedev_shared_t *local_pciedev_shared = bus->pcie_sh;
	struct bcmstrbuf strbuf;
	unsigned long flags;
	bool dongle_trap_occured = FALSE;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (DHD_NOCHECKDIED_ON()) {
		return 0;
	}

	if (data == NULL) {
		/*
		 * Called after a rx ctrl timeout. "data" is NULL.
		 * allocate memory to trace the trap or assert.
		 */
		size = msize;
		mbuffer = data = MALLOC(bus->dhd->osh, msize);

		if (mbuffer == NULL) {
			DHD_ERROR(("%s: MALLOC(%d) failed \n", __FUNCTION__, msize));
			bcmerror = BCME_NOMEM;
			goto done2;
		}
	}

	if ((str = MALLOC(bus->dhd->osh, maxstrlen)) == NULL) {
		DHD_ERROR(("%s: MALLOC(%d) failed \n", __FUNCTION__, maxstrlen));
		bcmerror = BCME_NOMEM;
		goto done2;
	}
	DHD_GENERAL_LOCK(bus->dhd, flags);
	DHD_BUS_BUSY_SET_IN_CHECKDIED(bus->dhd);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);

	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req(bus);
	}
	if ((bcmerror = dhdpcie_readshared(bus)) < 0) {
		goto done1;
	}

	bcm_binit(&strbuf, data, size);

	bcm_bprintf(&strbuf, "msgtrace address : 0x%08X\nconsole address  : 0x%08X\n",
	            local_pciedev_shared->msgtrace_addr, local_pciedev_shared->console_addr);

	if ((local_pciedev_shared->flags & PCIE_SHARED_ASSERT_BUILT) == 0) {
		/* NOTE: Misspelled assert is intentional - DO NOT FIX.
		 * (Avoids conflict with real asserts for programmatic parsing of output.)
		 */
		bcm_bprintf(&strbuf, "Assrt not built in dongle\n");
	}

	if ((bus->pcie_sh->flags & (PCIE_SHARED_ASSERT|PCIE_SHARED_TRAP)) == 0) {
		/* NOTE: Misspelled assert is intentional - DO NOT FIX.
		 * (Avoids conflict with real asserts for programmatic parsing of output.)
		 */
		bcm_bprintf(&strbuf, "No trap%s in dongle",
		          (bus->pcie_sh->flags & PCIE_SHARED_ASSERT_BUILT)
		          ?"/assrt" :"");
	} else {
		if (bus->pcie_sh->flags & PCIE_SHARED_ASSERT) {
			/* Download assert */
			bcm_bprintf(&strbuf, "Dongle assert");
			if (bus->pcie_sh->assert_exp_addr != 0) {
				str[0] = '\0';
				if ((bcmerror = dhdpcie_bus_membytes(bus, FALSE,
					bus->pcie_sh->assert_exp_addr,
					(uint8 *)str, maxstrlen)) < 0) {
					goto done1;
				}

				str[maxstrlen - 1] = '\0';
				bcm_bprintf(&strbuf, " expr \"%s\"", str);
			}

			if (bus->pcie_sh->assert_file_addr != 0) {
				str[0] = '\0';
				if ((bcmerror = dhdpcie_bus_membytes(bus, FALSE,
					bus->pcie_sh->assert_file_addr,
					(uint8 *)str, maxstrlen)) < 0) {
					goto done1;
				}

				str[maxstrlen - 1] = '\0';
				bcm_bprintf(&strbuf, " file \"%s\"", str);
			}

			bcm_bprintf(&strbuf, " line %d ",  bus->pcie_sh->assert_line);
		}

		if (bus->pcie_sh->flags & PCIE_SHARED_TRAP) {
			trap_t *tr = &bus->dhd->last_trap_info;
			dongle_trap_occured = TRUE;
			if ((bcmerror = dhdpcie_bus_membytes(bus, FALSE,
				bus->pcie_sh->trap_addr, (uint8*)tr, sizeof(trap_t))) < 0) {
				bus->dhd->dongle_trap_occured = TRUE;
				goto done1;
			}
			dhd_bus_dump_trap_info(bus, &strbuf);
		}
	}

	if (bus->pcie_sh->flags & (PCIE_SHARED_ASSERT | PCIE_SHARED_TRAP)) {
		DHD_FWLOG(("%s: %s\n", __FUNCTION__, strbuf.origbuf));

		dhd_bus_dump_console_buffer(bus);
		dhd_prot_debug_info_print(bus->dhd);

#if defined(DHD_FW_COREDUMP)
		/* save core dump or write to a file */
		if (bus->dhd->memdump_enabled) {
#ifdef DHD_SSSR_DUMP
			bus->dhd->collect_sssr = TRUE;
#endif /* DHD_SSSR_DUMP */
			bus->dhd->memdump_type = DUMP_TYPE_DONGLE_TRAP;
			dhdpcie_mem_dump(bus);
		}
#endif /* DHD_FW_COREDUMP */

		/* set the trap occured flag only after all the memdump,
		* logdump and sssr dump collection has been scheduled
		*/
		if (dongle_trap_occured) {
			bus->dhd->dongle_trap_occured = TRUE;
		}

		/* wake up IOCTL wait event */
		dhd_wakeup_ioctl_event(bus->dhd, IOCTL_RETURN_ON_TRAP);

#ifdef WL_CFGVENDOR_SEND_HANG_EVENT
		copy_hang_info_trap(bus->dhd);
#endif /* WL_CFGVENDOR_SEND_HANG_EVENT */
		dhd_schedule_reset(bus->dhd);

	}

done1:
	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req_clear(bus);
	}

	DHD_GENERAL_LOCK(bus->dhd, flags);
	DHD_BUS_BUSY_CLEAR_IN_CHECKDIED(bus->dhd);
	dhd_os_busbusy_wake(bus->dhd);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);
done2:
	if (mbuffer)
		MFREE(bus->dhd->osh, mbuffer, msize);
	if (str)
		MFREE(bus->dhd->osh, str, maxstrlen);

	return bcmerror;
} /* dhdpcie_checkdied */

/* Custom copy of dhdpcie_mem_dump() that can be called at interrupt level */
void dhdpcie_mem_dump_bugcheck(dhd_bus_t *bus, uint8 *buf)
{
	int ret = 0;
	int size; /* Full mem size */
	int start; /* Start address */
	int read_size = 0; /* Read size of each iteration */
	uint8 *databuf = buf;

	if (bus == NULL) {
		return;
	}

	start = bus->dongle_ram_base;
	read_size = 4;
	/* check for dead bus */
	{
		uint test_word = 0;
		ret = dhdpcie_bus_membytes(bus, FALSE, start, (uint8*)&test_word, read_size);
		/* if read error or bus timeout */
		if (ret || (test_word == 0xFFFFFFFF)) {
			return;
		}
	}

	/* Get full mem size */
	size = bus->ramsize;
	/* Read mem content */
	while (size)
	{
		read_size = MIN(MEMBLOCK, size);
		if ((ret = dhdpcie_bus_membytes(bus, FALSE, start, databuf, read_size))) {
			return;
		}

		/* Decrement size and increment start address */
		size -= read_size;
		start += read_size;
		databuf += read_size;
	}
	bus->dhd->soc_ram = buf;
	bus->dhd->soc_ram_length = bus->ramsize;
	return;
}

#if defined(DHD_FW_COREDUMP)
static int
dhdpcie_get_mem_dump(dhd_bus_t *bus)
{
	int ret = BCME_OK;
	int size = 0;
	int start = 0;
	int read_size = 0; /* Read size of each iteration */
	uint8 *p_buf = NULL, *databuf = NULL;

	if (!bus) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (!bus->dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	size = bus->ramsize; /* Full mem size */
	start = bus->dongle_ram_base; /* Start address */

	/* Get full mem size */
	p_buf = dhd_get_fwdump_buf(bus->dhd, size);
	if (!p_buf) {
		DHD_ERROR(("%s: Out of memory (%d bytes)\n",
			__FUNCTION__, size));
		return BCME_ERROR;
	}

	/* Read mem content */
	DHD_TRACE_HW4(("Dump dongle memory\n"));
	databuf = p_buf;
	while (size > 0) {
		read_size = MIN(MEMBLOCK, size);
		ret = dhdpcie_bus_membytes(bus, FALSE, start, databuf, read_size);
		if (ret) {
			DHD_ERROR(("%s: Error membytes %d\n", __FUNCTION__, ret));
#ifdef DHD_DEBUG_UART
			bus->dhd->memdump_success = FALSE;
#endif	/* DHD_DEBUG_UART */
			break;
		}
		DHD_TRACE(("."));

		/* Decrement size and increment start address */
		size -= read_size;
		start += read_size;
		databuf += read_size;
	}

	return ret;
}

static int
dhdpcie_mem_dump(dhd_bus_t *bus)
{
	dhd_pub_t *dhdp;
	int ret;

#ifdef EXYNOS_PCIE_DEBUG
	exynos_pcie_register_dump(1);
#endif /* EXYNOS_PCIE_DEBUG */

#ifdef SUPPORT_LINKDOWN_RECOVERY
	if (bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link is down so skip\n", __FUNCTION__));
		/* panic only for DUMP_MEMFILE_BUGON */
		ASSERT(bus->dhd->memdump_enabled != DUMP_MEMFILE_BUGON);
		return BCME_ERROR;
	}
#endif /* SUPPORT_LINKDOWN_RECOVERY */

	dhdp = bus->dhd;
	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(dhdp)) {
		DHD_ERROR(("%s: bus is down! can't collect mem dump. \n", __FUNCTION__));
		return BCME_ERROR;
	}

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	if (pm_runtime_get_sync(dhd_bus_to_dev(bus)) < 0)
		return BCME_ERROR;
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

	ret = dhdpcie_get_mem_dump(bus);
	if (ret) {
		DHD_ERROR(("%s: failed to get mem dump, err=%d\n",
			__FUNCTION__, ret));
		return ret;
	}

	dhd_schedule_memdump(dhdp, dhdp->soc_ram, dhdp->soc_ram_length);
	/* buf, actually soc_ram free handled in dhd_{free,clear} */

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	pm_runtime_mark_last_busy(dhd_bus_to_dev(bus));
	pm_runtime_put_autosuspend(dhd_bus_to_dev(bus));
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

	return ret;
}

int
dhd_bus_get_mem_dump(dhd_pub_t *dhdp)
{
	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	return dhdpcie_get_mem_dump(dhdp->bus);
}

int
dhd_bus_mem_dump(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = dhdp->bus;
	int ret = BCME_ERROR;

	if (dhdp->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s bus is down\n", __FUNCTION__));
		return BCME_ERROR;
	}

	/* Try to resume if already suspended or suspend in progress */
#ifdef DHD_PCIE_RUNTIMEPM
	dhdpcie_runtime_bus_wake(dhdp, CAN_SLEEP(), __builtin_return_address(0));
#endif /* DHD_PCIE_RUNTIMEPM */

	/* Skip if still in suspended or suspend in progress */
	if (DHD_BUS_CHECK_SUSPEND_OR_ANY_SUSPEND_IN_PROGRESS(dhdp)) {
		DHD_ERROR(("%s: bus is in suspend(%d) or suspending(0x%x) state, so skip\n",
			__FUNCTION__, dhdp->busstate, dhdp->dhd_bus_busy_state));
		return BCME_ERROR;
	}

	DHD_OS_WAKE_LOCK(dhdp);
	ret = dhdpcie_mem_dump(bus);
	DHD_OS_WAKE_UNLOCK(dhdp);
	return ret;
}
#endif	/* DHD_FW_COREDUMP */

int
dhd_socram_dump(dhd_bus_t *bus)
{
#if defined(DHD_FW_COREDUMP)
	DHD_OS_WAKE_LOCK(bus->dhd);
	dhd_bus_mem_dump(bus->dhd);
	DHD_OS_WAKE_UNLOCK(bus->dhd);
	return 0;
#else
	return -1;
#endif // endif
}

/**
 * Transfers bytes from host to dongle using pio mode.
 * Parameter 'address' is a backplane address.
 */
static int
dhdpcie_bus_membytes(dhd_bus_t *bus, bool write, ulong address, uint8 *data, uint size)
{
	uint dsize;
	int detect_endian_flag = 0x01;
	bool little_endian;

	if (write && bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link was down\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req(bus);
	}
	/* Detect endianness. */
	little_endian = *(char *)&detect_endian_flag;

	/* In remap mode, adjust address beyond socram and redirect
	 * to devram at SOCDEVRAM_BP_ADDR since remap address > orig_ramsize
	 * is not backplane accessible
	 */

	/* Determine initial transfer parameters */
#ifdef DHD_SUPPORT_64BIT
	dsize = sizeof(uint64);
#else /* !DHD_SUPPORT_64BIT */
	dsize = sizeof(uint32);
#endif /* DHD_SUPPORT_64BIT */

	/* Do the transfer(s) */
	if (write) {
		while (size) {
#ifdef DHD_SUPPORT_64BIT
			if (size >= sizeof(uint64) && little_endian &&	!(address % 8)) {
				dhdpcie_bus_wtcm64(bus, address, *((uint64 *)data));
			}
#else /* !DHD_SUPPORT_64BIT */
			if (size >= sizeof(uint32) && little_endian &&	!(address % 4)) {
				dhdpcie_bus_wtcm32(bus, address, *((uint32*)data));
			}
#endif /* DHD_SUPPORT_64BIT */
			else {
				dsize = sizeof(uint8);
				dhdpcie_bus_wtcm8(bus, address, *data);
			}

			/* Adjust for next transfer (if any) */
			if ((size -= dsize)) {
				data += dsize;
				address += dsize;
			}
		}
	} else {
		while (size) {
#ifdef DHD_SUPPORT_64BIT
			if (size >= sizeof(uint64) && little_endian &&	!(address % 8))
			{
				*(uint64 *)data = dhdpcie_bus_rtcm64(bus, address);
			}
#else /* !DHD_SUPPORT_64BIT */
			if (size >= sizeof(uint32) && little_endian &&	!(address % 4))
			{
				*(uint32 *)data = dhdpcie_bus_rtcm32(bus, address);
			}
#endif /* DHD_SUPPORT_64BIT */
			else {
				dsize = sizeof(uint8);
				*data = dhdpcie_bus_rtcm8(bus, address);
			}

			/* Adjust for next transfer (if any) */
			if ((size -= dsize) > 0) {
				data += dsize;
				address += dsize;
			}
		}
	}
	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req_clear(bus);
	}
	return BCME_OK;
} /* dhdpcie_bus_membytes */

/**
 * Transfers one transmit (ethernet) packet that was queued in the (flow controlled) flow ring queue
 * to the (non flow controlled) flow ring.
 */
int BCMFASTPATH
dhd_bus_schedule_queue(struct dhd_bus  *bus, uint16 flow_id, bool txs)
{
	flow_ring_node_t *flow_ring_node;
	int ret = BCME_OK;
#ifdef DHD_LOSSLESS_ROAMING
	dhd_pub_t *dhdp = bus->dhd;
#endif // endif
	DHD_INFO(("%s: flow_id is %d\n", __FUNCTION__, flow_id));

	/* ASSERT on flow_id */
	if (flow_id >= bus->max_submission_rings) {
		DHD_ERROR(("%s: flow_id is invalid %d, max %d\n", __FUNCTION__,
			flow_id, bus->max_submission_rings));
		return 0;
	}

	flow_ring_node = DHD_FLOW_RING(bus->dhd, flow_id);

	if (flow_ring_node->prot_info == NULL) {
	    DHD_ERROR((" %s : invalid flow_ring_node \n", __FUNCTION__));
	    return BCME_NOTREADY;
	}

#ifdef DHD_LOSSLESS_ROAMING
	if ((dhdp->dequeue_prec_map & (1 << flow_ring_node->flow_info.tid)) == 0) {
		DHD_INFO(("%s: tid %d is not in precedence map. block scheduling\n",
			__FUNCTION__, flow_ring_node->flow_info.tid));
		return BCME_OK;
	}
#endif /* DHD_LOSSLESS_ROAMING */

	{
		unsigned long flags;
		void *txp = NULL;
		flow_queue_t *queue;
#ifdef DHD_LOSSLESS_ROAMING
		struct ether_header *eh;
		uint8 *pktdata;
#endif /* DHD_LOSSLESS_ROAMING */

		queue = &flow_ring_node->queue; /* queue associated with flow ring */

		DHD_FLOWRING_LOCK(flow_ring_node->lock, flags);

		if (flow_ring_node->status != FLOW_RING_STATUS_OPEN) {
			DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);
			return BCME_NOTREADY;
		}

		while ((txp = dhd_flow_queue_dequeue(bus->dhd, queue)) != NULL) {
			PKTORPHAN(txp);

			/*
			 * Modifying the packet length caused P2P cert failures.
			 * Specifically on test cases where a packet of size 52 bytes
			 * was injected, the sniffer capture showed 62 bytes because of
			 * which the cert tests failed. So making the below change
			 * only Router specific.
			 */

#ifdef DHDTCPACK_SUPPRESS
			if (bus->dhd->tcpack_sup_mode != TCPACK_SUP_HOLD) {
				ret = dhd_tcpack_check_xmit(bus->dhd, txp);
				if (ret != BCME_OK) {
					DHD_ERROR(("%s: dhd_tcpack_check_xmit() error.\n",
						__FUNCTION__));
				}
			}
#endif /* DHDTCPACK_SUPPRESS */
#ifdef DHD_LOSSLESS_ROAMING
			pktdata = (uint8 *)PKTDATA(OSH_NULL, txp);
			eh = (struct ether_header *) pktdata;
			if (eh->ether_type == hton16(ETHER_TYPE_802_1X)) {
				uint8 prio = (uint8)PKTPRIO(txp);
				/* Restore to original priority for 802.1X packet */
				if (prio == PRIO_8021D_NC) {
					PKTSETPRIO(txp, dhdp->prio_8021x);
				}
			}
#endif /* DHD_LOSSLESS_ROAMING */
			/* Attempt to transfer packet over flow ring */
			ret = dhd_prot_txdata(bus->dhd, txp, flow_ring_node->flow_info.ifindex);
			if (ret != BCME_OK) { /* may not have resources in flow ring */
				DHD_INFO(("%s: Reinserrt %d\n", __FUNCTION__, ret));
				dhd_prot_txdata_write_flush(bus->dhd, flow_id);
				/* reinsert at head */
				dhd_flow_queue_reinsert(bus->dhd, queue, txp);
				DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);

				/* If we are able to requeue back, return success */
				return BCME_OK;
			}
		}

#ifdef DHD_HP2P
		if (!flow_ring_node->hp2p_ring) {
			dhd_prot_txdata_write_flush(bus->dhd, flow_id);
		}
#else
		dhd_prot_txdata_write_flush(bus->dhd, flow_id);
#endif // endif
		DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);
	}

	return ret;
} /* dhd_bus_schedule_queue */

/** Sends an (ethernet) data frame (in 'txp') to the dongle. Callee disposes of txp. */
int BCMFASTPATH
dhd_bus_txdata(struct dhd_bus *bus, void *txp, uint8 ifidx)
{
	uint16 flowid;
#ifdef IDLE_TX_FLOW_MGMT
	uint8	node_status;
#endif /* IDLE_TX_FLOW_MGMT */
	flow_queue_t *queue;
	flow_ring_node_t *flow_ring_node;
	unsigned long flags;
	int ret = BCME_OK;
	void *txp_pend = NULL;

	if (!bus->dhd->flowid_allocator) {
		DHD_ERROR(("%s: Flow ring not intited yet  \n", __FUNCTION__));
		goto toss;
	}

	flowid = DHD_PKT_GET_FLOWID(txp);

	flow_ring_node = DHD_FLOW_RING(bus->dhd, flowid);

	DHD_TRACE(("%s: pkt flowid %d, status %d active %d\n",
		__FUNCTION__, flowid, flow_ring_node->status, flow_ring_node->active));

	DHD_FLOWRING_LOCK(flow_ring_node->lock, flags);
	if ((flowid >= bus->dhd->num_flow_rings) ||
#ifdef IDLE_TX_FLOW_MGMT
		(!flow_ring_node->active))
#else
		(!flow_ring_node->active) ||
		(flow_ring_node->status == FLOW_RING_STATUS_DELETE_PENDING) ||
		(flow_ring_node->status == FLOW_RING_STATUS_STA_FREEING))
#endif /* IDLE_TX_FLOW_MGMT */
	{
		DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);
		DHD_INFO(("%s: Dropping pkt flowid %d, status %d active %d\n",
			__FUNCTION__, flowid, flow_ring_node->status,
			flow_ring_node->active));
		ret = BCME_ERROR;
			goto toss;
	}

#ifdef IDLE_TX_FLOW_MGMT
	node_status = flow_ring_node->status;

	/* handle diffrent status states here!! */
	switch (node_status)
	{
		case FLOW_RING_STATUS_OPEN:

			if (bus->enable_idle_flowring_mgmt) {
				/* Move the node to the head of active list */
				dhd_flow_ring_move_to_active_list_head(bus, flow_ring_node);
			}
			break;

		case FLOW_RING_STATUS_SUSPENDED:
			DHD_INFO(("Need to Initiate TX Flow resume\n"));
			/* Issue resume_ring request */
			dhd_bus_flow_ring_resume_request(bus,
					flow_ring_node);
			break;

		case FLOW_RING_STATUS_CREATE_PENDING:
		case FLOW_RING_STATUS_RESUME_PENDING:
			/* Dont do anything here!! */
			DHD_INFO(("Waiting for Flow create/resume! status is %u\n",
				node_status));
			break;

		case FLOW_RING_STATUS_DELETE_PENDING:
		default:
			DHD_ERROR(("Dropping packet!! flowid %u status is %u\n",
				flowid, node_status));
			/* error here!! */
			ret = BCME_ERROR;
			DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);
			goto toss;
	}
	/* Now queue the packet */
#endif /* IDLE_TX_FLOW_MGMT */

	queue = &flow_ring_node->queue; /* queue associated with flow ring */

	if ((ret = dhd_flow_queue_enqueue(bus->dhd, queue, txp)) != BCME_OK)
		txp_pend = txp;

	DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);

	if (flow_ring_node->status) {
		DHD_INFO(("%s: Enq pkt flowid %d, status %d active %d\n",
		    __FUNCTION__, flowid, flow_ring_node->status,
		    flow_ring_node->active));
		if (txp_pend) {
			txp = txp_pend;
			goto toss;
		}
		return BCME_OK;
	}
	ret = dhd_bus_schedule_queue(bus, flowid, FALSE); /* from queue to flowring */

	/* If we have anything pending, try to push into q */
	if (txp_pend) {
		DHD_FLOWRING_LOCK(flow_ring_node->lock, flags);

		if ((ret = dhd_flow_queue_enqueue(bus->dhd, queue, txp_pend)) != BCME_OK) {
			DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);
			txp = txp_pend;
			goto toss;
		}

		DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);
	}

	return ret;

toss:
	DHD_INFO(("%s: Toss %d\n", __FUNCTION__, ret));
	PKTCFREE(bus->dhd->osh, txp, TRUE);
	return ret;
} /* dhd_bus_txdata */

void
dhd_bus_stop_queue(struct dhd_bus *bus)
{
	dhd_txflowcontrol(bus->dhd, ALL_INTERFACES, ON);
}

void
dhd_bus_start_queue(struct dhd_bus *bus)
{
	/*
	 * Tx queue has been stopped due to resource shortage (or)
	 * bus is not in a state to turn on.
	 *
	 * Note that we try to re-start network interface only
	 * when we have enough resources, one has to first change the
	 * flag indicating we have all the resources.
	 */
	if (dhd_prot_check_tx_resource(bus->dhd)) {
		DHD_ERROR(("%s: Interface NOT started, previously stopped "
			"due to resource shortage\n", __FUNCTION__));
		return;
	}
	dhd_txflowcontrol(bus->dhd, ALL_INTERFACES, OFF);
}

/* Device console input function */
int dhd_bus_console_in(dhd_pub_t *dhd, uchar *msg, uint msglen)
{
	dhd_bus_t *bus = dhd->bus;
	uint32 addr, val;
	int rv;
	/* Address could be zero if CONSOLE := 0 in dongle Makefile */
	if (bus->console_addr == 0)
		return BCME_UNSUPPORTED;

	/* Don't allow input if dongle is in reset */
	if (bus->dhd->dongle_reset) {
		return BCME_NOTREADY;
	}

	/* Zero cbuf_index */
	addr = bus->console_addr + OFFSETOF(hnd_cons_t, cbuf_idx);
	val = htol32(0);
	if ((rv = dhdpcie_bus_membytes(bus, TRUE, addr, (uint8 *)&val, sizeof(val))) < 0)
		goto done;

	/* Write message into cbuf */
	addr = bus->console_addr + OFFSETOF(hnd_cons_t, cbuf);
	if ((rv = dhdpcie_bus_membytes(bus, TRUE, addr, (uint8 *)msg, msglen)) < 0)
		goto done;

	/* Write length into vcons_in */
	addr = bus->console_addr + OFFSETOF(hnd_cons_t, vcons_in);
	val = htol32(msglen);
	if ((rv = dhdpcie_bus_membytes(bus, TRUE, addr, (uint8 *)&val, sizeof(val))) < 0)
		goto done;

	/* generate an interrupt to dongle to indicate that it needs to process cons command */
	dhdpcie_send_mb_data(bus, H2D_HOST_CONS_INT);
done:
	return rv;
} /* dhd_bus_console_in */

/**
 * Called on frame reception, the frame was received from the dongle on interface 'ifidx' and is
 * contained in 'pkt'. Processes rx frame, forwards up the layer to netif.
 */
void BCMFASTPATH
dhd_bus_rx_frame(struct dhd_bus *bus, void* pkt, int ifidx, uint pkt_count)
{
	dhd_rx_frame(bus->dhd, ifidx, pkt, pkt_count, 0);
}

void
dhdpcie_setbar1win(dhd_bus_t *bus, uint32 addr)
{
	dhdpcie_os_setbar1win(bus, addr);
}

/** 'offset' is a backplane address */
void
dhdpcie_bus_wtcm8(dhd_bus_t *bus, ulong offset, uint8 data)
{
	if (bus->is_linkdown) {
		DHD_LOG_MEM(("%s: PCIe link was down\n", __FUNCTION__));
		return;
	} else {
		dhdpcie_os_wtcm8(bus, offset, data);
	}
}

uint8
dhdpcie_bus_rtcm8(dhd_bus_t *bus, ulong offset)
{
	volatile uint8 data;
	if (bus->is_linkdown) {
		DHD_LOG_MEM(("%s: PCIe link was down\n", __FUNCTION__));
		data = (uint8)-1;
	} else {
		data = dhdpcie_os_rtcm8(bus, offset);
	}
	return data;
}

void
dhdpcie_bus_wtcm32(dhd_bus_t *bus, ulong offset, uint32 data)
{
	if (bus->is_linkdown) {
		DHD_LOG_MEM(("%s: PCIe link was down\n", __FUNCTION__));
		return;
	} else {
		dhdpcie_os_wtcm32(bus, offset, data);
	}
}
void
dhdpcie_bus_wtcm16(dhd_bus_t *bus, ulong offset, uint16 data)
{
	if (bus->is_linkdown) {
		DHD_LOG_MEM(("%s: PCIe link was down\n", __FUNCTION__));
		return;
	} else {
		dhdpcie_os_wtcm16(bus, offset, data);
	}
}
#ifdef DHD_SUPPORT_64BIT
void
dhdpcie_bus_wtcm64(dhd_bus_t *bus, ulong offset, uint64 data)
{
	if (bus->is_linkdown) {
		DHD_LOG_MEM(("%s: PCIe link was down\n", __FUNCTION__));
		return;
	} else {
		dhdpcie_os_wtcm64(bus, offset, data);
	}
}
#endif /* DHD_SUPPORT_64BIT */

uint16
dhdpcie_bus_rtcm16(dhd_bus_t *bus, ulong offset)
{
	volatile uint16 data;
	if (bus->is_linkdown) {
		DHD_LOG_MEM(("%s: PCIe link was down\n", __FUNCTION__));
		data = (uint16)-1;
	} else {
		data = dhdpcie_os_rtcm16(bus, offset);
	}
	return data;
}

uint32
dhdpcie_bus_rtcm32(dhd_bus_t *bus, ulong offset)
{
	volatile uint32 data;
	if (bus->is_linkdown) {
		DHD_LOG_MEM(("%s: PCIe link was down\n", __FUNCTION__));
		data = (uint32)-1;
	} else {
		data = dhdpcie_os_rtcm32(bus, offset);
	}
	return data;
}

#ifdef DHD_SUPPORT_64BIT
uint64
dhdpcie_bus_rtcm64(dhd_bus_t *bus, ulong offset)
{
	volatile uint64 data;
	if (bus->is_linkdown) {
		DHD_LOG_MEM(("%s: PCIe link was down\n", __FUNCTION__));
		data = (uint64)-1;
	} else {
		data = dhdpcie_os_rtcm64(bus, offset);
	}
	return data;
}
#endif /* DHD_SUPPORT_64BIT */

/** A snippet of dongle memory is shared between host and dongle */
void
dhd_bus_cmn_writeshared(dhd_bus_t *bus, void *data, uint32 len, uint8 type, uint16 ringid)
{
	uint64 long_data;
	ulong addr; /* dongle address */

	DHD_INFO(("%s: writing to dongle type %d len %d\n", __FUNCTION__, type, len));

	if (bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link was down\n", __FUNCTION__));
		return;
	}

	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req(bus);
	}
	switch (type) {
		case D2H_DMA_SCRATCH_BUF:
			addr = DHD_PCIE_SHARED_MEMBER_ADDR(bus, host_dma_scratch_buffer);
			long_data = HTOL64(*(uint64 *)data);
			dhdpcie_bus_membytes(bus, TRUE, addr, (uint8*) &long_data, len);
			if (dhd_msg_level & DHD_INFO_VAL) {
				prhex(__FUNCTION__, data, len);
			}
			break;

		case D2H_DMA_SCRATCH_BUF_LEN :
			addr = DHD_PCIE_SHARED_MEMBER_ADDR(bus, host_dma_scratch_buffer_len);
			dhdpcie_bus_wtcm32(bus, addr, (uint32) HTOL32(*(uint32 *)data));
			if (dhd_msg_level & DHD_INFO_VAL) {
				prhex(__FUNCTION__, data, len);
			}
			break;

		case H2D_DMA_INDX_WR_BUF:
			long_data = HTOL64(*(uint64 *)data);
			addr = DHD_RING_INFO_MEMBER_ADDR(bus, h2d_w_idx_hostaddr);
			dhdpcie_bus_membytes(bus, TRUE, addr, (uint8*) &long_data, len);
			if (dhd_msg_level & DHD_INFO_VAL) {
				prhex(__FUNCTION__, data, len);
			}
			break;

		case H2D_DMA_INDX_RD_BUF:
			long_data = HTOL64(*(uint64 *)data);
			addr = DHD_RING_INFO_MEMBER_ADDR(bus, h2d_r_idx_hostaddr);
			dhdpcie_bus_membytes(bus, TRUE, addr, (uint8*) &long_data, len);
			if (dhd_msg_level & DHD_INFO_VAL) {
				prhex(__FUNCTION__, data, len);
			}
			break;

		case D2H_DMA_INDX_WR_BUF:
			long_data = HTOL64(*(uint64 *)data);
			addr = DHD_RING_INFO_MEMBER_ADDR(bus, d2h_w_idx_hostaddr);
			dhdpcie_bus_membytes(bus, TRUE, addr, (uint8*) &long_data, len);
			if (dhd_msg_level & DHD_INFO_VAL) {
				prhex(__FUNCTION__, data, len);
			}
			break;

		case D2H_DMA_INDX_RD_BUF:
			long_data = HTOL64(*(uint64 *)data);
			addr = DHD_RING_INFO_MEMBER_ADDR(bus, d2h_r_idx_hostaddr);
			dhdpcie_bus_membytes(bus, TRUE, addr, (uint8*) &long_data, len);
			if (dhd_msg_level & DHD_INFO_VAL) {
				prhex(__FUNCTION__, data, len);
			}
			break;

		case H2D_IFRM_INDX_WR_BUF:
			long_data = HTOL64(*(uint64 *)data);
			addr = DHD_RING_INFO_MEMBER_ADDR(bus, ifrm_w_idx_hostaddr);
			dhdpcie_bus_membytes(bus, TRUE, addr, (uint8*) &long_data, len);
			if (dhd_msg_level & DHD_INFO_VAL) {
				prhex(__FUNCTION__, data, len);
			}
			break;

		case RING_ITEM_LEN :
			addr = DHD_RING_MEM_MEMBER_ADDR(bus, ringid, len_items);
			dhdpcie_bus_wtcm16(bus, addr, (uint16) HTOL16(*(uint16 *)data));
			break;

		case RING_MAX_ITEMS :
			addr = DHD_RING_MEM_MEMBER_ADDR(bus, ringid, max_item);
			dhdpcie_bus_wtcm16(bus, addr, (uint16) HTOL16(*(uint16 *)data));
			break;

		case RING_BUF_ADDR :
			long_data = HTOL64(*(uint64 *)data);
			addr = DHD_RING_MEM_MEMBER_ADDR(bus, ringid, base_addr);
			dhdpcie_bus_membytes(bus, TRUE, addr, (uint8 *) &long_data, len);
			if (dhd_msg_level & DHD_INFO_VAL) {
				prhex(__FUNCTION__, data, len);
			}
			break;

		case RING_WR_UPD :
			addr = bus->ring_sh[ringid].ring_state_w;
			dhdpcie_bus_wtcm16(bus, addr, (uint16) HTOL16(*(uint16 *)data));
			break;

		case RING_RD_UPD :
			addr = bus->ring_sh[ringid].ring_state_r;
			dhdpcie_bus_wtcm16(bus, addr, (uint16) HTOL16(*(uint16 *)data));
			break;

		case D2H_MB_DATA:
			addr = bus->d2h_mb_data_ptr_addr;
			dhdpcie_bus_wtcm32(bus, addr, (uint32) HTOL32(*(uint32 *)data));
			break;

		case H2D_MB_DATA:
			addr = bus->h2d_mb_data_ptr_addr;
			dhdpcie_bus_wtcm32(bus, addr, (uint32) HTOL32(*(uint32 *)data));
			break;

		case HOST_API_VERSION:
			addr = DHD_PCIE_SHARED_MEMBER_ADDR(bus, host_cap);
			dhdpcie_bus_wtcm32(bus, addr, (uint32) HTOL32(*(uint32 *)data));
			break;

		case DNGL_TO_HOST_TRAP_ADDR:
			long_data = HTOL64(*(uint64 *)data);
			addr = DHD_PCIE_SHARED_MEMBER_ADDR(bus, host_trap_addr);
			dhdpcie_bus_membytes(bus, TRUE, addr, (uint8 *) &long_data, len);
			DHD_INFO(("Wrote trap addr:0x%x\n", (uint32) HTOL32(*(uint32 *)data)));
			break;

		case HOST_SCB_ADDR:
			addr = DHD_PCIE_SHARED_MEMBER_ADDR(bus, host_scb_addr);
#ifdef DHD_SUPPORT_64BIT
			dhdpcie_bus_wtcm64(bus, addr, (uint64) HTOL64(*(uint64 *)data));
#else /* !DHD_SUPPORT_64BIT */
			dhdpcie_bus_wtcm32(bus, addr, *((uint32*)data));
#endif /* DHD_SUPPORT_64BIT */
			DHD_INFO(("Wrote host_scb_addr:0x%x\n",
				(uint32) HTOL32(*(uint32 *)data)));
			break;

		default:
			break;
	}
	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req_clear(bus);
	}
} /* dhd_bus_cmn_writeshared */

/** A snippet of dongle memory is shared between host and dongle */
void
dhd_bus_cmn_readshared(dhd_bus_t *bus, void* data, uint8 type, uint16 ringid)
{
	ulong addr; /* dongle address */

	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req(bus);
	}
	switch (type) {
		case RING_WR_UPD :
			addr = bus->ring_sh[ringid].ring_state_w;
			*(uint16*)data = LTOH16(dhdpcie_bus_rtcm16(bus, addr));
			break;

		case RING_RD_UPD :
			addr = bus->ring_sh[ringid].ring_state_r;
			*(uint16*)data = LTOH16(dhdpcie_bus_rtcm16(bus, addr));
			break;

		case TOTAL_LFRAG_PACKET_CNT :
			addr = DHD_PCIE_SHARED_MEMBER_ADDR(bus, total_lfrag_pkt_cnt);
			*(uint16*)data = LTOH16(dhdpcie_bus_rtcm16(bus, addr));
			break;

		case H2D_MB_DATA:
			addr = bus->h2d_mb_data_ptr_addr;
			*(uint32*)data = LTOH32(dhdpcie_bus_rtcm32(bus, addr));
			break;

		case D2H_MB_DATA:
			addr = bus->d2h_mb_data_ptr_addr;
			*(uint32*)data = LTOH32(dhdpcie_bus_rtcm32(bus, addr));
			break;

		case MAX_HOST_RXBUFS :
			addr = DHD_PCIE_SHARED_MEMBER_ADDR(bus, max_host_rxbufs);
			*(uint16*)data = LTOH16(dhdpcie_bus_rtcm16(bus, addr));
			break;

		case HOST_SCB_ADDR:
			addr = DHD_PCIE_SHARED_MEMBER_ADDR(bus, host_scb_size);
			*(uint32*)data = LTOH32(dhdpcie_bus_rtcm32(bus, addr));
			break;

		default :
			break;
	}
	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req_clear(bus);
	}
}

uint32 dhd_bus_get_sharedflags(dhd_bus_t *bus)
{
	return ((pciedev_shared_t*)bus->pcie_sh)->flags;
}

void
dhd_bus_clearcounts(dhd_pub_t *dhdp)
{
}

/**
 * @param params    input buffer, NULL for 'set' operation.
 * @param plen      length of 'params' buffer, 0 for 'set' operation.
 * @param arg       output buffer
 */
int
dhd_bus_iovar_op(dhd_pub_t *dhdp, const char *name,
                 void *params, int plen, void *arg, int len, bool set)
{
	dhd_bus_t *bus = dhdp->bus;
	const bcm_iovar_t *vi = NULL;
	int bcmerror = BCME_UNSUPPORTED;
	int val_size;
	uint32 actionid;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(name);
	ASSERT(len >= 0);
	if (!name || len < 0)
		return BCME_BADARG;

	/* Get MUST have return space */
	ASSERT(set || (arg && len));
	if (!(set || (arg && len)))
		return BCME_BADARG;

	/* Set does NOT take qualifiers */
	ASSERT(!set || (!params && !plen));
	if (!(!set || (!params && !plen)))
		return BCME_BADARG;

	DHD_INFO(("%s: %s %s, len %d plen %d\n", __FUNCTION__,
	         name, (set ? "set" : "get"), len, plen));

	/* Look up var locally; if not found pass to host driver */
	if ((vi = bcm_iovar_lookup(dhdpcie_iovars, name)) == NULL) {
		goto exit;
	}

	if (MULTIBP_ENAB(bus->sih)) {
		if (vi->flags & DHD_IOVF_PWRREQ_BYPASS) {
			DHD_ERROR(("%s: Bypass pwr request\n", __FUNCTION__));
		} else {
			dhd_bus_pcie_pwr_req(bus);
		}
	}

	/* set up 'params' pointer in case this is a set command so that
	 * the convenience int and bool code can be common to set and get
	 */
	if (params == NULL) {
		params = arg;
		plen = len;
	}

	if (vi->type == IOVT_VOID)
		val_size = 0;
	else if (vi->type == IOVT_BUFFER)
		val_size = len;
	else
		/* all other types are integer sized */
		val_size = sizeof(int);

	actionid = set ? IOV_SVAL(vi->varid) : IOV_GVAL(vi->varid);
	bcmerror = dhdpcie_bus_doiovar(bus, vi, actionid, name, params, plen, arg, len, val_size);

exit:
	/* In DEVRESET_QUIESCE/DEVRESET_ON,
	 * this includes dongle re-attach which initialize pwr_req_ref count to 0 and
	 * causes pwr_req_ref count miss-match in pwr req clear function and hang.
	 * In this case, bypass pwr req clear.
	 */
	if (bcmerror == BCME_DNGL_DEVRESET) {
		bcmerror = BCME_OK;
	} else {
		if (MULTIBP_ENAB(bus->sih)) {
			if (vi->flags & DHD_IOVF_PWRREQ_BYPASS) {
				DHD_ERROR(("%s: Bypass pwr request clear\n", __FUNCTION__));
			} else {
				dhd_bus_pcie_pwr_req_clear(bus);
			}
		}
	}
	return bcmerror;
} /* dhd_bus_iovar_op */

#ifdef BCM_BUZZZ
#include <bcm_buzzz.h>

int
dhd_buzzz_dump_cntrs(char *p, uint32 *core, uint32 *log,
	const int num_counters)
{
	int bytes = 0;
	uint32 ctr;
	uint32 curr[BCM_BUZZZ_COUNTERS_MAX], prev[BCM_BUZZZ_COUNTERS_MAX];
	uint32 delta[BCM_BUZZZ_COUNTERS_MAX];

	/* Compute elapsed counter values per counter event type */
	for (ctr = 0U; ctr < num_counters; ctr++) {
		prev[ctr] = core[ctr];
		curr[ctr] = *log++;
		core[ctr] = curr[ctr];  /* saved for next log */

		if (curr[ctr] < prev[ctr])
			delta[ctr] = curr[ctr] + (~0U - prev[ctr]);
		else
			delta[ctr] = (curr[ctr] - prev[ctr]);

		bytes += sprintf(p + bytes, "%12u ", delta[ctr]);
	}

	return bytes;
}

typedef union cm3_cnts { /* export this in bcm_buzzz.h */
	uint32 u32;
	uint8  u8[4];
	struct {
		uint8 cpicnt;
		uint8 exccnt;
		uint8 sleepcnt;
		uint8 lsucnt;
	};
} cm3_cnts_t;

int
dhd_bcm_buzzz_dump_cntrs6(char *p, uint32 *core, uint32 *log)
{
	int bytes = 0;

	uint32 cyccnt, instrcnt;
	cm3_cnts_t cm3_cnts;
	uint8 foldcnt;

	{   /* 32bit cyccnt */
		uint32 curr, prev, delta;
		prev = core[0]; curr = *log++; core[0] = curr;
		if (curr < prev)
			delta = curr + (~0U - prev);
		else
			delta = (curr - prev);

		bytes += sprintf(p + bytes, "%12u ", delta);
		cyccnt = delta;
	}

	{	/* Extract the 4 cnts: cpi, exc, sleep and lsu */
		int i;
		uint8 max8 = ~0;
		cm3_cnts_t curr, prev, delta;
		prev.u32 = core[1]; curr.u32 = * log++; core[1] = curr.u32;
		for (i = 0; i < 4; i++) {
			if (curr.u8[i] < prev.u8[i])
				delta.u8[i] = curr.u8[i] + (max8 - prev.u8[i]);
			else
				delta.u8[i] = (curr.u8[i] - prev.u8[i]);
			bytes += sprintf(p + bytes, "%4u ", delta.u8[i]);
		}
		cm3_cnts.u32 = delta.u32;
	}

	{   /* Extract the foldcnt from arg0 */
		uint8 curr, prev, delta, max8 = ~0;
		bcm_buzzz_arg0_t arg0; arg0.u32 = *log;
		prev = core[2]; curr = arg0.klog.cnt; core[2] = curr;
		if (curr < prev)
			delta = curr + (max8 - prev);
		else
			delta = (curr - prev);
		bytes += sprintf(p + bytes, "%4u ", delta);
		foldcnt = delta;
	}

	instrcnt = cyccnt - (cm3_cnts.u8[0] + cm3_cnts.u8[1] + cm3_cnts.u8[2]
		                 + cm3_cnts.u8[3]) + foldcnt;
	if (instrcnt > 0xFFFFFF00)
		bytes += sprintf(p + bytes, "[%10s] ", "~");
	else
		bytes += sprintf(p + bytes, "[%10u] ", instrcnt);
	return bytes;
}

int
dhd_buzzz_dump_log(char *p, uint32 *core, uint32 *log, bcm_buzzz_t *buzzz)
{
	int bytes = 0;
	bcm_buzzz_arg0_t arg0;
	static uint8 * fmt[] = BCM_BUZZZ_FMT_STRINGS;

	if (buzzz->counters == 6) {
		bytes += dhd_bcm_buzzz_dump_cntrs6(p, core, log);
		log += 2; /* 32bit cyccnt + (4 x 8bit) CM3 */
	} else {
		bytes += dhd_buzzz_dump_cntrs(p, core, log, buzzz->counters);
		log += buzzz->counters; /* (N x 32bit) CR4=3, CA7=4 */
	}

	/* Dump the logged arguments using the registered formats */
	arg0.u32 = *log++;

	switch (arg0.klog.args) {
		case 0:
			bytes += sprintf(p + bytes, fmt[arg0.klog.id]);
			break;
		case 1:
		{
			uint32 arg1 = *log++;
			bytes += sprintf(p + bytes, fmt[arg0.klog.id], arg1);
			break;
		}
		case 2:
		{
			uint32 arg1, arg2;
			arg1 = *log++; arg2 = *log++;
			bytes += sprintf(p + bytes, fmt[arg0.klog.id], arg1, arg2);
			break;
		}
		case 3:
		{
			uint32 arg1, arg2, arg3;
			arg1 = *log++; arg2 = *log++; arg3 = *log++;
			bytes += sprintf(p + bytes, fmt[arg0.klog.id], arg1, arg2, arg3);
			break;
		}
		case 4:
		{
			uint32 arg1, arg2, arg3, arg4;
			arg1 = *log++; arg2 = *log++;
			arg3 = *log++; arg4 = *log++;
			bytes += sprintf(p + bytes, fmt[arg0.klog.id], arg1, arg2, arg3, arg4);
			break;
		}
		default:
			printf("Maximum one argument supported\n");
			break;
	}

	bytes += sprintf(p + bytes, "\n");

	return bytes;
}

void dhd_buzzz_dump(bcm_buzzz_t *buzzz_p, void *buffer_p, char *p)
{
	int i;
	uint32 total, part1, part2, log_sz, core[BCM_BUZZZ_COUNTERS_MAX];
	void * log;

	for (i = 0; i < BCM_BUZZZ_COUNTERS_MAX; i++) {
		core[i] = 0;
	}

	log_sz = buzzz_p->log_sz;

	part1 = ((uint32)buzzz_p->cur - (uint32)buzzz_p->log) / log_sz;

	if (buzzz_p->wrap == TRUE) {
		part2 = ((uint32)buzzz_p->end - (uint32)buzzz_p->cur) / log_sz;
		total = (buzzz_p->buffer_sz - BCM_BUZZZ_LOGENTRY_MAXSZ) / log_sz;
	} else {
		part2 = 0U;
		total = buzzz_p->count;
	}

	if (total == 0U) {
		printf("bcm_buzzz_dump total<%u> done\n", total);
		return;
	} else {
		printf("bcm_buzzz_dump total<%u> : part2<%u> + part1<%u>\n",
		       total, part2, part1);
	}

	if (part2) {   /* with wrap */
		log = (void*)((size_t)buffer_p + (buzzz_p->cur - buzzz_p->log));
		while (part2--) {   /* from cur to end : part2 */
			p[0] = '\0';
			dhd_buzzz_dump_log(p, core, (uint32 *)log, buzzz_p);
			printf("%s", p);
			log = (void*)((size_t)log + buzzz_p->log_sz);
		}
	}

	log = (void*)buffer_p;
	while (part1--) {
		p[0] = '\0';
		dhd_buzzz_dump_log(p, core, (uint32 *)log, buzzz_p);
		printf("%s", p);
		log = (void*)((size_t)log + buzzz_p->log_sz);
	}

	printf("bcm_buzzz_dump done.\n");
}

int dhd_buzzz_dump_dngl(dhd_bus_t *bus)
{
	bcm_buzzz_t * buzzz_p = NULL;
	void * buffer_p = NULL;
	char * page_p = NULL;
	pciedev_shared_t *sh;
	int ret = 0;

	if (bus->dhd->busstate != DHD_BUS_DATA) {
		return BCME_UNSUPPORTED;
	}
	if ((page_p = (char *)MALLOC(bus->dhd->osh, 4096)) == NULL) {
		printf("Page memory allocation failure\n");
		goto done;
	}
	if ((buzzz_p = MALLOC(bus->dhd->osh, sizeof(bcm_buzzz_t))) == NULL) {
		printf("BCM BUZZZ memory allocation failure\n");
		goto done;
	}

	ret = dhdpcie_readshared(bus);
	if (ret < 0) {
		DHD_ERROR(("%s :Shared area read failed \n", __FUNCTION__));
		goto done;
	}

	sh = bus->pcie_sh;

	DHD_INFO(("%s buzzz:%08x\n", __FUNCTION__, sh->buzz_dbg_ptr));

	if (sh->buzz_dbg_ptr != 0U) {	/* Fetch and display dongle BUZZZ Trace */

		dhdpcie_bus_membytes(bus, FALSE, (ulong)sh->buzz_dbg_ptr,
		                     (uint8 *)buzzz_p, sizeof(bcm_buzzz_t));

		printf("BUZZZ[0x%08x]: log<0x%08x> cur<0x%08x> end<0x%08x> "
			"count<%u> status<%u> wrap<%u>\n"
			"cpu<0x%02X> counters<%u> group<%u> buffer_sz<%u> log_sz<%u>\n",
			(int)sh->buzz_dbg_ptr,
			(int)buzzz_p->log, (int)buzzz_p->cur, (int)buzzz_p->end,
			buzzz_p->count, buzzz_p->status, buzzz_p->wrap,
			buzzz_p->cpu_idcode, buzzz_p->counters, buzzz_p->group,
			buzzz_p->buffer_sz, buzzz_p->log_sz);

		if (buzzz_p->count == 0) {
			printf("Empty dongle BUZZZ trace\n\n");
			goto done;
		}

		/* Allocate memory for trace buffer and format strings */
		buffer_p = MALLOC(bus->dhd->osh, buzzz_p->buffer_sz);
		if (buffer_p == NULL) {
			printf("Buffer memory allocation failure\n");
			goto done;
		}

		/* Fetch the trace. format strings are exported via bcm_buzzz.h */
		dhdpcie_bus_membytes(bus, FALSE, (uint32)buzzz_p->log,   /* Trace */
		                     (uint8 *)buffer_p, buzzz_p->buffer_sz);

		/* Process and display the trace using formatted output */

		{
			int ctr;
			for (ctr = 0; ctr < buzzz_p->counters; ctr++) {
				printf("<Evt[%02X]> ", buzzz_p->eventid[ctr]);
			}
			printf("<code execution point>\n");
		}

		dhd_buzzz_dump(buzzz_p, buffer_p, page_p);

		printf("----- End of dongle BCM BUZZZ Trace -----\n\n");

		MFREE(bus->dhd->osh, buffer_p, buzzz_p->buffer_sz); buffer_p = NULL;
	}

done:

	if (page_p)   MFREE(bus->dhd->osh, page_p, 4096);
	if (buzzz_p)  MFREE(bus->dhd->osh, buzzz_p, sizeof(bcm_buzzz_t));
	if (buffer_p) MFREE(bus->dhd->osh, buffer_p, buzzz_p->buffer_sz);

	return BCME_OK;
}
#endif /* BCM_BUZZZ */

#define PCIE_GEN2(sih) ((BUSTYPE((sih)->bustype) == PCI_BUS) &&	\
	((sih)->buscoretype == PCIE2_CORE_ID))
#ifdef DHD_PCIE_REG_ACCESS
static bool
pcie2_mdiosetblock(dhd_bus_t *bus, uint blk)
{
	uint mdiodata, mdioctrl, i = 0;
	uint pcie_serdes_spinwait = 200;

	mdioctrl = MDIOCTL2_DIVISOR_VAL | (0x1F << MDIOCTL2_REGADDR_SHF);
	mdiodata = (blk << MDIODATA2_DEVADDR_SHF) | MDIODATA2_DONE;

	si_corereg(bus->sih, bus->sih->buscoreidx, PCIE2_MDIO_CONTROL, ~0, mdioctrl);
	si_corereg(bus->sih, bus->sih->buscoreidx, PCIE2_MDIO_WR_DATA, ~0, mdiodata);

	OSL_DELAY(10);
	/* retry till the transaction is complete */
	while (i < pcie_serdes_spinwait) {
		uint mdioctrl_read = si_corereg(bus->sih, bus->sih->buscoreidx, PCIE2_MDIO_WR_DATA,
			0, 0);
		if (!(mdioctrl_read & MDIODATA2_DONE)) {
			break;
		}
		OSL_DELAY(1000);
		i++;
	}

	if (i >= pcie_serdes_spinwait) {
		DHD_ERROR(("pcie_mdiosetblock: timed out\n"));
		return FALSE;
	}

	return TRUE;
}
#endif /* DHD_PCIE_REG_ACCESS */

#define PCIE_FLR_CAPAB_BIT		28
#define PCIE_FUNCTION_LEVEL_RESET_BIT	15

/* Change delays for only QT HW, FPGA and silicon uses same delay */
#ifdef BCMQT_HW
#define DHD_FUNCTION_LEVEL_RESET_DELAY		300000u
#define DHD_SSRESET_STATUS_RETRY_DELAY	10000u
#else
#define DHD_FUNCTION_LEVEL_RESET_DELAY	70u	/* 70 msec delay */
#define DHD_SSRESET_STATUS_RETRY_DELAY	40u
#endif // endif
/*
 * Increase SSReset de-assert time to 8ms.
 * since it takes longer time if re-scan time on 4378B0.
 */
#define DHD_SSRESET_STATUS_RETRIES	200u

static void
dhdpcie_enum_reg_init(dhd_bus_t *bus)
{
	/* initialize Function control register (clear bit 4) to HW init value */
	si_corereg(bus->sih, bus->sih->buscoreidx,
		OFFSETOF(sbpcieregs_t, ftn_ctrl.control), ~0,
		PCIE_CPLCA_ENABLE | PCIE_DLY_PERST_TO_COE);

	/* clear IntMask */
	si_corereg(bus->sih, bus->sih->buscoreidx,
		OFFSETOF(sbpcieregs_t, ftn_ctrl.intmask), ~0, 0);
	/* clear IntStatus */
	si_corereg(bus->sih, bus->sih->buscoreidx,
		OFFSETOF(sbpcieregs_t, ftn_ctrl.intstatus), ~0,
		si_corereg(bus->sih, bus->sih->buscoreidx,
			OFFSETOF(sbpcieregs_t, ftn_ctrl.intstatus), 0, 0));

	/* clear MSIVector */
	si_corereg(bus->sih, bus->sih->buscoreidx,
		OFFSETOF(sbpcieregs_t, ftn_ctrl.msi_vector), ~0, 0);
	/* clear MSIIntMask */
	si_corereg(bus->sih, bus->sih->buscoreidx,
		OFFSETOF(sbpcieregs_t, ftn_ctrl.msi_intmask), ~0, 0);
	/* clear MSIIntStatus */
	si_corereg(bus->sih, bus->sih->buscoreidx,
		OFFSETOF(sbpcieregs_t, ftn_ctrl.msi_intstatus), ~0,
		si_corereg(bus->sih, bus->sih->buscoreidx,
			OFFSETOF(sbpcieregs_t, ftn_ctrl.msi_intstatus), 0, 0));

	/* clear PowerIntMask */
	si_corereg(bus->sih, bus->sih->buscoreidx,
		OFFSETOF(sbpcieregs_t, ftn_ctrl.pwr_intmask), ~0, 0);
	/* clear PowerIntStatus */
	si_corereg(bus->sih, bus->sih->buscoreidx,
		OFFSETOF(sbpcieregs_t, ftn_ctrl.pwr_intstatus), ~0,
		si_corereg(bus->sih, bus->sih->buscoreidx,
			OFFSETOF(sbpcieregs_t, ftn_ctrl.pwr_intstatus), 0, 0));

	/* clear MailboxIntMask */
	si_corereg(bus->sih, bus->sih->buscoreidx,
		OFFSETOF(sbpcieregs_t, ftn_ctrl.mbox_intmask), ~0, 0);
	/* clear MailboxInt */
	si_corereg(bus->sih, bus->sih->buscoreidx,
		OFFSETOF(sbpcieregs_t, ftn_ctrl.mbox_intstatus), ~0,
		si_corereg(bus->sih, bus->sih->buscoreidx,
			OFFSETOF(sbpcieregs_t, ftn_ctrl.mbox_intstatus), 0, 0));
}

int
dhd_bus_perform_flr(dhd_bus_t *bus, bool force_fail)
{
	uint flr_capab;
	uint val;
	int retry = 0;

	DHD_ERROR(("******** Perform FLR ********\n"));

	if (PCIE_ENUM_RESET_WAR_ENAB(bus->sih->buscorerev)) {
		if (bus->pcie_mailbox_mask != 0) {
			dhdpcie_bus_intr_disable(bus);
		}
		/* initialize F0 enum registers before FLR for rev66/67 */
		dhdpcie_enum_reg_init(bus);
	}

	/* Read PCIE_CFG_DEVICE_CAPABILITY bit 28 to check FLR capability */
	val = OSL_PCI_READ_CONFIG(bus->osh, PCIE_CFG_DEVICE_CAPABILITY, sizeof(val));
	flr_capab =  val & (1 << PCIE_FLR_CAPAB_BIT);
	DHD_INFO(("Read Device Capability: reg=0x%x read val=0x%x flr_capab=0x%x\n",
		PCIE_CFG_DEVICE_CAPABILITY, val, flr_capab));
	if (!flr_capab) {
	       DHD_ERROR(("Chip does not support FLR\n"));
	       return BCME_UNSUPPORTED;
	}
	/* WAR: Disable FLR reset  For H2 chip to perform legacy reset */
	else if ((bus->sih->chip == CYW55560_CHIP_ID) || (bus->sih->chip == BCM4375_CHIP_ID)) {
		DHD_INFO(("H2/4375 CHIP return unsupported\n"));
		return BCME_UNSUPPORTED;
	}

	/* Save pcie config space */
	DHD_INFO(("Save Pcie Config Space\n"));
	DHD_PCIE_CONFIG_SAVE(bus);

	/* Set bit 15 of PCIE_CFG_DEVICE_CONTROL */
	DHD_INFO(("Set PCIE_FUNCTION_LEVEL_RESET_BIT(%d) of PCIE_CFG_DEVICE_CONTROL(0x%x)\n",
		PCIE_FUNCTION_LEVEL_RESET_BIT, PCIE_CFG_DEVICE_CONTROL));
	val = OSL_PCI_READ_CONFIG(bus->osh, PCIE_CFG_DEVICE_CONTROL, sizeof(val));
	DHD_INFO(("read_config: reg=0x%x read val=0x%x\n", PCIE_CFG_DEVICE_CONTROL, val));
	val = val | (1 << PCIE_FUNCTION_LEVEL_RESET_BIT);
	DHD_INFO(("write_config: reg=0x%x write val=0x%x\n", PCIE_CFG_DEVICE_CONTROL, val));
	OSL_PCI_WRITE_CONFIG(bus->osh, PCIE_CFG_DEVICE_CONTROL, sizeof(val), val);

	/* wait for DHD_FUNCTION_LEVEL_RESET_DELAY msec */
	DHD_INFO(("Delay of %d msec\n", DHD_FUNCTION_LEVEL_RESET_DELAY));
	OSL_DELAY(DHD_FUNCTION_LEVEL_RESET_DELAY * 1000u);

	if (force_fail) {
		DHD_ERROR(("Set PCIE_SSRESET_DISABLE_BIT(%d) of PCIE_CFG_SUBSYSTEM_CONTROL(0x%x)\n",
			PCIE_SSRESET_DISABLE_BIT, PCIE_CFG_SUBSYSTEM_CONTROL));
		val = OSL_PCI_READ_CONFIG(bus->osh, PCIE_CFG_SUBSYSTEM_CONTROL, sizeof(val));
		DHD_ERROR(("read_config: reg=0x%x read val=0x%x\n", PCIE_CFG_SUBSYSTEM_CONTROL,
			val));
		val = val | (1 << PCIE_SSRESET_DISABLE_BIT);
		DHD_ERROR(("write_config: reg=0x%x write val=0x%x\n", PCIE_CFG_SUBSYSTEM_CONTROL,
			val));
		OSL_PCI_WRITE_CONFIG(bus->osh, PCIE_CFG_SUBSYSTEM_CONTROL, sizeof(val), val);

		val = OSL_PCI_READ_CONFIG(bus->osh, PCIE_CFG_SUBSYSTEM_CONTROL, sizeof(val));
		DHD_ERROR(("read_config: reg=0x%x read val=0x%x\n", PCIE_CFG_SUBSYSTEM_CONTROL,
			val));
	}

	/* Clear bit 15 of PCIE_CFG_DEVICE_CONTROL */
	DHD_INFO(("Clear PCIE_FUNCTION_LEVEL_RESET_BIT(%d) of PCIE_CFG_DEVICE_CONTROL(0x%x)\n",
		PCIE_FUNCTION_LEVEL_RESET_BIT, PCIE_CFG_DEVICE_CONTROL));
	val = OSL_PCI_READ_CONFIG(bus->osh, PCIE_CFG_DEVICE_CONTROL, sizeof(val));
	DHD_INFO(("read_config: reg=0x%x read val=0x%x\n", PCIE_CFG_DEVICE_CONTROL, val));
	val = val & ~(1 << PCIE_FUNCTION_LEVEL_RESET_BIT);
	DHD_INFO(("write_config: reg=0x%x write val=0x%x\n", PCIE_CFG_DEVICE_CONTROL, val));
	OSL_PCI_WRITE_CONFIG(bus->osh, PCIE_CFG_DEVICE_CONTROL, sizeof(val), val);

	/* Wait till bit 13 of PCIE_CFG_SUBSYSTEM_CONTROL is cleared */
	DHD_INFO(("Wait till PCIE_SSRESET_STATUS_BIT(%d) of PCIE_CFG_SUBSYSTEM_CONTROL(0x%x)"
		"is cleared\n",	PCIE_SSRESET_STATUS_BIT, PCIE_CFG_SUBSYSTEM_CONTROL));
	do {
		val = OSL_PCI_READ_CONFIG(bus->osh, PCIE_CFG_SUBSYSTEM_CONTROL, sizeof(val));
		DHD_ERROR(("read_config: reg=0x%x read val=0x%x\n",
			PCIE_CFG_SUBSYSTEM_CONTROL, val));
		val = val & (1 << PCIE_SSRESET_STATUS_BIT);
		OSL_DELAY(DHD_SSRESET_STATUS_RETRY_DELAY);
	} while (val && (retry++ < DHD_SSRESET_STATUS_RETRIES));

	if (val) {
		DHD_ERROR(("ERROR: reg=0x%x bit %d is not cleared\n",
			PCIE_CFG_SUBSYSTEM_CONTROL, PCIE_SSRESET_STATUS_BIT));
		/* User has to fire the IOVAR again, if force_fail is needed */
		if (force_fail) {
			bus->flr_force_fail = FALSE;
			DHD_ERROR(("%s cleared flr_force_fail flag\n", __FUNCTION__));
		}
		return BCME_DONGLE_DOWN;
	}

	/* Restore pcie config space */
	DHD_INFO(("Restore Pcie Config Space\n"));
	DHD_PCIE_CONFIG_RESTORE(bus);

	DHD_ERROR(("******** FLR Succedeed ********\n"));

	return BCME_OK;
}

#ifdef DHD_USE_BP_RESET
#define DHD_BP_RESET_ASPM_DISABLE_DELAY	500u	/* usec */

#define DHD_BP_RESET_STATUS_RETRY_DELAY	40u	/* usec */
#define DHD_BP_RESET_STATUS_RETRIES	50u

#define PCIE_CFG_SPROM_CTRL_SB_RESET_BIT	10
#define PCIE_CFG_CLOCK_CTRL_STATUS_BP_RESET_BIT	21
int
dhd_bus_perform_bp_reset(struct dhd_bus *bus)
{
	uint val;
	int retry = 0;
	uint dar_clk_ctrl_status_reg = DAR_CLK_CTRL(bus->sih->buscorerev);
	int ret = BCME_OK;
	bool cond;

	DHD_ERROR(("******** Perform BP reset ********\n"));

	/* Disable ASPM */
	DHD_INFO(("Disable ASPM: Clear bits(1-0) of PCIECFGREG_LINK_STATUS_CTRL(0x%x)\n",
		PCIECFGREG_LINK_STATUS_CTRL));
	val = OSL_PCI_READ_CONFIG(bus->osh, PCIECFGREG_LINK_STATUS_CTRL, sizeof(val));
	DHD_INFO(("read_config: reg=0x%x read val=0x%x\n", PCIECFGREG_LINK_STATUS_CTRL, val));
	val = val & (~PCIE_ASPM_ENAB);
	DHD_INFO(("write_config: reg=0x%x write val=0x%x\n", PCIECFGREG_LINK_STATUS_CTRL, val));
	OSL_PCI_WRITE_CONFIG(bus->osh, PCIECFGREG_LINK_STATUS_CTRL, sizeof(val), val);

	/* wait for delay usec */
	DHD_INFO(("Delay of %d usec\n", DHD_BP_RESET_ASPM_DISABLE_DELAY));
	OSL_DELAY(DHD_BP_RESET_ASPM_DISABLE_DELAY);

	/* Set bit 10 of PCIECFGREG_SPROM_CTRL */
	DHD_INFO(("Set PCIE_CFG_SPROM_CTRL_SB_RESET_BIT(%d) of PCIECFGREG_SPROM_CTRL(0x%x)\n",
		PCIE_CFG_SPROM_CTRL_SB_RESET_BIT, PCIECFGREG_SPROM_CTRL));
	val = OSL_PCI_READ_CONFIG(bus->osh, PCIECFGREG_SPROM_CTRL, sizeof(val));
	DHD_INFO(("read_config: reg=0x%x read val=0x%x\n", PCIECFGREG_SPROM_CTRL, val));
	val = val | (1 << PCIE_CFG_SPROM_CTRL_SB_RESET_BIT);
	DHD_INFO(("write_config: reg=0x%x write val=0x%x\n", PCIECFGREG_SPROM_CTRL, val));
	OSL_PCI_WRITE_CONFIG(bus->osh, PCIECFGREG_SPROM_CTRL, sizeof(val), val);

	/* Wait till bit backplane reset is ASSERTED i,e
	 * bit 10 of PCIECFGREG_SPROM_CTRL is cleared.
	 * Only after this, poll for 21st bit of DAR reg 0xAE0 is valid
	 * else DAR register will read previous old value
	 */
	DHD_INFO(("Wait till PCIE_CFG_SPROM_CTRL_SB_RESET_BIT(%d) of "
		"PCIECFGREG_SPROM_CTRL(0x%x) is cleared\n",
		PCIE_CFG_SPROM_CTRL_SB_RESET_BIT, PCIECFGREG_SPROM_CTRL));
	do {
		val = OSL_PCI_READ_CONFIG(bus->osh, PCIECFGREG_SPROM_CTRL, sizeof(val));
		DHD_INFO(("read_config: reg=0x%x read val=0x%x\n", PCIECFGREG_SPROM_CTRL, val));
		cond = val & (1 << PCIE_CFG_SPROM_CTRL_SB_RESET_BIT);
		OSL_DELAY(DHD_BP_RESET_STATUS_RETRY_DELAY);
	} while (cond && (retry++ < DHD_BP_RESET_STATUS_RETRIES));

	if (cond) {
		DHD_ERROR(("ERROR: reg=0x%x bit %d is not cleared\n",
			PCIECFGREG_SPROM_CTRL, PCIE_CFG_SPROM_CTRL_SB_RESET_BIT));
		ret = BCME_ERROR;
		goto aspm_enab;
	}

	/* Wait till bit 21 of dar_clk_ctrl_status_reg is cleared */
	DHD_INFO(("Wait till PCIE_CFG_CLOCK_CTRL_STATUS_BP_RESET_BIT(%d) of "
		"dar_clk_ctrl_status_reg(0x%x) is cleared\n",
		PCIE_CFG_CLOCK_CTRL_STATUS_BP_RESET_BIT, dar_clk_ctrl_status_reg));
	do {
		val = si_corereg(bus->sih, bus->sih->buscoreidx,
			dar_clk_ctrl_status_reg, 0, 0);
		DHD_INFO(("read_dar si_corereg: reg=0x%x read val=0x%x\n",
			dar_clk_ctrl_status_reg, val));
		cond = val & (1 << PCIE_CFG_CLOCK_CTRL_STATUS_BP_RESET_BIT);
		OSL_DELAY(DHD_BP_RESET_STATUS_RETRY_DELAY);
	} while (cond && (retry++ < DHD_BP_RESET_STATUS_RETRIES));

	if (cond) {
		DHD_ERROR(("ERROR: reg=0x%x bit %d is not cleared\n",
			dar_clk_ctrl_status_reg, PCIE_CFG_CLOCK_CTRL_STATUS_BP_RESET_BIT));
		ret = BCME_ERROR;
	}

aspm_enab:
	/* Enable ASPM */
	DHD_INFO(("Enable ASPM: set bit 1 of PCIECFGREG_LINK_STATUS_CTRL(0x%x)\n",
		PCIECFGREG_LINK_STATUS_CTRL));
	val = OSL_PCI_READ_CONFIG(bus->osh, PCIECFGREG_LINK_STATUS_CTRL, sizeof(val));
	DHD_INFO(("read_config: reg=0x%x read val=0x%x\n", PCIECFGREG_LINK_STATUS_CTRL, val));
	val = val | (PCIE_ASPM_L1_ENAB);
	DHD_INFO(("write_config: reg=0x%x write val=0x%x\n", PCIECFGREG_LINK_STATUS_CTRL, val));
	OSL_PCI_WRITE_CONFIG(bus->osh, PCIECFGREG_LINK_STATUS_CTRL, sizeof(val), val);

	DHD_ERROR(("******** BP reset Succedeed ********\n"));

	return ret;
}
#endif /* DHD_USE_BP_RESET */

int
dhd_bus_devreset(dhd_pub_t *dhdp, uint8 flag)
{
	dhd_bus_t *bus = dhdp->bus;
	int bcmerror = 0;
	unsigned long flags;
	unsigned long flags_bus;
#ifdef CONFIG_ARCH_MSM
	int retry = POWERUP_MAX_RETRY;
#endif /* CONFIG_ARCH_MSM */

	if (flag == TRUE) { /* Turn off WLAN */
		/* Removing Power */
		DHD_ERROR(("%s: == Power OFF ==\n", __FUNCTION__));
		DHD_ERROR(("%s: making dhdpub up FALSE\n", __FUNCTION__));
		bus->dhd->up = FALSE;

		/* wait for other contexts to finish -- if required a call
		* to OSL_DELAY for 1s can be added to give other contexts
		* a chance to finish
		*/
		dhdpcie_advertise_bus_cleanup(bus->dhd);

		if (bus->dhd->busstate != DHD_BUS_DOWN) {
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
			atomic_set(&bus->dhd->block_bus, TRUE);
			dhd_flush_rx_tx_wq(bus->dhd);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

#ifdef BCMPCIE_OOB_HOST_WAKE
			/* Clean up any pending host wake IRQ */
			dhd_bus_oob_intr_set(bus->dhd, FALSE);
			dhd_bus_oob_intr_unregister(bus->dhd);
#endif /* BCMPCIE_OOB_HOST_WAKE */
			dhd_os_wd_timer(dhdp, 0);
			dhd_bus_stop(bus, TRUE);
			if (bus->intr) {
				DHD_BUS_LOCK(bus->bus_lock, flags_bus);
				dhdpcie_bus_intr_disable(bus);
				DHD_BUS_UNLOCK(bus->bus_lock, flags_bus);
				dhdpcie_free_irq(bus);
			}
			dhd_deinit_bus_lock(bus);
			dhd_deinit_backplane_access_lock(bus);
			dhd_bus_release_dongle(bus);
			dhdpcie_bus_free_resource(bus);
			bcmerror = dhdpcie_bus_disable_device(bus);
			if (bcmerror) {
				DHD_ERROR(("%s: dhdpcie_bus_disable_device: %d\n",
					__FUNCTION__, bcmerror));
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
				atomic_set(&bus->dhd->block_bus, FALSE);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
			}
			/* Clean up protocol data after Bus Master Enable bit clear
			 * so that host can safely unmap DMA and remove the allocated buffers
			 * from the PKTID MAP. Some Applicantion Processors supported
			 * System MMU triggers Kernel panic when they detect to attempt to
			 * DMA-unmapped memory access from the devices which use the
			 * System MMU. Therefore, Kernel panic can be happened since it is
			 * possible that dongle can access to DMA-unmapped memory after
			 * calling the dhd_prot_reset().
			 * For this reason, the dhd_prot_reset() and dhd_clear() functions
			 * should be located after the dhdpcie_bus_disable_device().
			 */
			dhd_prot_reset(dhdp);
			dhd_clear(dhdp);
#ifdef CONFIG_ARCH_MSM
			bcmerror = dhdpcie_bus_clock_stop(bus);
			if (bcmerror) {
				DHD_ERROR(("%s: host clock stop failed: %d\n",
					__FUNCTION__, bcmerror));
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
				atomic_set(&bus->dhd->block_bus, FALSE);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
				goto done;
			}
#endif /* CONFIG_ARCH_MSM */
			DHD_GENERAL_LOCK(bus->dhd, flags);
			DHD_ERROR(("%s: making DHD_BUS_DOWN\n", __FUNCTION__));
			bus->dhd->busstate = DHD_BUS_DOWN;
			DHD_GENERAL_UNLOCK(bus->dhd, flags);
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
			atomic_set(&bus->dhd->block_bus, FALSE);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
		} else {
			if (bus->intr) {
				dhdpcie_free_irq(bus);
			}
#ifdef BCMPCIE_OOB_HOST_WAKE
			/* Clean up any pending host wake IRQ */
			dhd_bus_oob_intr_set(bus->dhd, FALSE);
			dhd_bus_oob_intr_unregister(bus->dhd);
#endif /* BCMPCIE_OOB_HOST_WAKE */
			dhd_dpc_kill(bus->dhd);
			if (!bus->no_bus_init) {
				dhd_bus_release_dongle(bus);
				dhdpcie_bus_free_resource(bus);
				bcmerror = dhdpcie_bus_disable_device(bus);
				if (bcmerror) {
					DHD_ERROR(("%s: dhdpcie_bus_disable_device: %d\n",
						__FUNCTION__, bcmerror));
				}

				/* Clean up protocol data after Bus Master Enable bit clear
				 * so that host can safely unmap DMA and remove the allocated
				 * buffers from the PKTID MAP. Some Applicantion Processors
				 * supported System MMU triggers Kernel panic when they detect
				 * to attempt to DMA-unmapped memory access from the devices
				 * which use the System MMU.
				 * Therefore, Kernel panic can be happened since it is possible
				 * that dongle can access to DMA-unmapped memory after calling
				 * the dhd_prot_reset().
				 * For this reason, the dhd_prot_reset() and dhd_clear() functions
				 * should be located after the dhdpcie_bus_disable_device().
				 */
				dhd_prot_reset(dhdp);
				dhd_clear(dhdp);
			} else {
				bus->no_bus_init = FALSE;
			}
#ifdef CONFIG_ARCH_MSM
			bcmerror = dhdpcie_bus_clock_stop(bus);
			if (bcmerror) {
				DHD_ERROR(("%s: host clock stop failed: %d\n",
					__FUNCTION__, bcmerror));
				goto done;
			}
#endif  /* CONFIG_ARCH_MSM */
		}

		bus->dhd->dongle_reset = TRUE;
		DHD_ERROR(("%s:  WLAN OFF Done\n", __FUNCTION__));

	} else { /* Turn on WLAN */
		if (bus->dhd->busstate == DHD_BUS_DOWN) {
			/* Powering On */
			DHD_ERROR(("%s: == Power ON ==\n", __FUNCTION__));
#ifdef CONFIG_ARCH_MSM
			while (--retry) {
				bcmerror = dhdpcie_bus_clock_start(bus);
				if (!bcmerror) {
					DHD_ERROR(("%s: dhdpcie_bus_clock_start OK\n",
						__FUNCTION__));
					break;
				} else {
					OSL_SLEEP(10);
				}
			}

			if (bcmerror && !retry) {
				DHD_ERROR(("%s: host pcie clock enable failed: %d\n",
					__FUNCTION__, bcmerror));
				goto done;
			}
#if defined(DHD_CONTROL_PCIE_ASPM_WIFI_TURNON)
			dhd_bus_aspm_enable_rc_ep(bus, FALSE);
#endif /* DHD_CONTROL_PCIE_ASPM_WIFI_TURNON */
#endif /* CONFIG_ARCH_MSM */
			bus->is_linkdown = 0;
			bus->cto_triggered = 0;
#ifdef SUPPORT_LINKDOWN_RECOVERY
			bus->read_shm_fail = FALSE;
#endif /* SUPPORT_LINKDOWN_RECOVERY */
			bcmerror = dhdpcie_bus_enable_device(bus);
			if (bcmerror) {
				DHD_ERROR(("%s: host configuration restore failed: %d\n",
					__FUNCTION__, bcmerror));
				goto done;
			}

			bcmerror = dhdpcie_bus_alloc_resource(bus);
			if (bcmerror) {
				DHD_ERROR(("%s: dhdpcie_bus_resource_alloc failed: %d\n",
					__FUNCTION__, bcmerror));
				goto done;
			}

			bcmerror = dhdpcie_bus_dongle_attach(bus);
			if (bcmerror) {
				DHD_ERROR(("%s: dhdpcie_bus_dongle_attach failed: %d\n",
					__FUNCTION__, bcmerror));
				goto done;
			}

			bcmerror = dhd_bus_request_irq(bus);
			if (bcmerror) {
				DHD_ERROR(("%s: dhd_bus_request_irq failed: %d\n",
					__FUNCTION__, bcmerror));
				goto done;
			}

			bus->dhd->dongle_reset = FALSE;

#if defined(DHD_CONTROL_PCIE_CPUCORE_WIFI_TURNON)
			dhd_irq_set_affinity(bus->dhd, cpumask_of(1));
#endif /* DHD_CONTROL_PCIE_CPUCORE_WIFI_TURNON */

			bcmerror = dhd_bus_start(dhdp);
			if (bcmerror) {
				DHD_ERROR(("%s: dhd_bus_start: %d\n",
					__FUNCTION__, bcmerror));
				goto done;
			}

			bus->dhd->up = TRUE;
			/* Renabling watchdog which is disabled in dhdpcie_advertise_bus_cleanup */
			if (bus->dhd->dhd_watchdog_ms_backup) {
				DHD_ERROR(("%s: Enabling wdtick after dhd init\n",
					__FUNCTION__));
				dhd_os_wd_timer(bus->dhd, bus->dhd->dhd_watchdog_ms_backup);
			}
			DHD_ERROR(("%s: WLAN Power On Done\n", __FUNCTION__));
		} else {
			DHD_ERROR(("%s: what should we do here\n", __FUNCTION__));
			goto done;
		}
	}

done:
	if (bcmerror) {
		DHD_GENERAL_LOCK(bus->dhd, flags);
		DHD_ERROR(("%s: making DHD_BUS_DOWN\n", __FUNCTION__));
		bus->dhd->busstate = DHD_BUS_DOWN;
		DHD_GENERAL_UNLOCK(bus->dhd, flags);
	}
	return bcmerror;
}

#ifdef DHD_PCIE_REG_ACCESS
static int
pcie2_mdioop(dhd_bus_t *bus, uint physmedia, uint regaddr, bool write, uint *val,
	bool slave_bypass)
{
	uint pcie_serdes_spinwait = 200, i = 0, mdio_ctrl;
	uint32 reg32;

	pcie2_mdiosetblock(bus, physmedia);

	/* enable mdio access to SERDES */
	mdio_ctrl = MDIOCTL2_DIVISOR_VAL;
	mdio_ctrl |= (regaddr << MDIOCTL2_REGADDR_SHF);

	if (slave_bypass)
		mdio_ctrl |= MDIOCTL2_SLAVE_BYPASS;

	if (!write)
		mdio_ctrl |= MDIOCTL2_READ;

	si_corereg(bus->sih, bus->sih->buscoreidx, PCIE2_MDIO_CONTROL, ~0, mdio_ctrl);

	if (write) {
		reg32 =  PCIE2_MDIO_WR_DATA;
		si_corereg(bus->sih, bus->sih->buscoreidx, PCIE2_MDIO_WR_DATA, ~0,
			*val | MDIODATA2_DONE);
	} else
		reg32 =  PCIE2_MDIO_RD_DATA;

	/* retry till the transaction is complete */
	while (i < pcie_serdes_spinwait) {
		uint done_val =  si_corereg(bus->sih, bus->sih->buscoreidx, reg32, 0, 0);
		if (!(done_val & MDIODATA2_DONE)) {
			if (!write) {
				*val = si_corereg(bus->sih, bus->sih->buscoreidx,
					PCIE2_MDIO_RD_DATA, 0, 0);
				*val = *val & MDIODATA2_MASK;
			}
			return 0;
		}
		OSL_DELAY(1000);
		i++;
	}
	return -1;
}
#endif /* DHD_PCIE_REG_ACCESS */

/* si_backplane_access() manages a shared resource - BAR0 mapping, hence its
 * calls shall be serialized. This wrapper function provides such serialization
 * and shall be used everywjer einstead of direct call of si_backplane_access()
 *
 * Linux DHD driver calls si_backplane_access() from 3 three contexts: tasklet
 * (that may call dhdpcie_sssr_dump() from dhdpcie_sssr_dump()), iovar
 * ("sbreg", "membyres", etc.) and procfs (used by GDB proxy). To avoid race
 * conditions calls of si_backplane_access() shall be serialized. Presence of
 * tasklet context implies that serialization shall b ebased on spinlock. Hence
 * Linux implementation of dhd_pcie_backplane_access_[un]lock() is
 * spinlock-based.
 *
 * Other platforms may add their own implementations of
 * dhd_pcie_backplane_access_[un]lock() as needed (e.g. if serialization is not
 * needed implementation might be empty)
 */
static uint
serialized_backplane_access(dhd_bus_t *bus, uint addr, uint size, uint *val, bool read)
{
	uint ret;
	unsigned long flags;
	DHD_BACKPLANE_ACCESS_LOCK(bus->backplane_access_lock, flags);
	ret = si_backplane_access(bus->sih, addr, size, val, read);
	DHD_BACKPLANE_ACCESS_UNLOCK(bus->backplane_access_lock, flags);
	return ret;
}

static int
dhdpcie_get_dma_ring_indices(dhd_pub_t *dhd)
{
	int h2d_support, d2h_support;

	d2h_support = dhd->dma_d2h_ring_upd_support ? 1 : 0;
	h2d_support = dhd->dma_h2d_ring_upd_support ? 1 : 0;
	return (d2h_support | (h2d_support << 1));

}
int
dhdpcie_set_dma_ring_indices(dhd_pub_t *dhd, int32 int_val)
{
	int bcmerror = 0;
	/* Can change it only during initialization/FW download */
	if (dhd->busstate == DHD_BUS_DOWN) {
		if ((int_val > 3) || (int_val < 0)) {
			DHD_ERROR(("Bad argument. Possible values: 0, 1, 2 & 3\n"));
			bcmerror = BCME_BADARG;
		} else {
			dhd->dma_d2h_ring_upd_support = (int_val & 1) ? TRUE : FALSE;
			dhd->dma_h2d_ring_upd_support = (int_val & 2) ? TRUE : FALSE;
			dhd->dma_ring_upd_overwrite = TRUE;
		}
	} else {
		DHD_ERROR(("%s: Can change only when bus down (before FW download)\n",
			__FUNCTION__));
		bcmerror = BCME_NOTDOWN;
	}

	return bcmerror;

}
/**
 * IOVAR handler of the DHD bus layer (in this case, the PCIe bus).
 *
 * @param actionid  e.g. IOV_SVAL(IOV_PCIEREG)
 * @param params    input buffer
 * @param plen      length in [bytes] of input buffer 'params'
 * @param arg       output buffer
 * @param len       length in [bytes] of output buffer 'arg'
 */
static int
dhdpcie_bus_doiovar(dhd_bus_t *bus, const bcm_iovar_t *vi, uint32 actionid, const char *name,
                void *params, int plen, void *arg, int len, int val_size)
{
	int bcmerror = 0;
	int32 int_val = 0;
	int32 int_val2 = 0;
	int32 int_val3 = 0;
	bool bool_val = 0;

	DHD_TRACE(("%s: Enter, action %d name %s params %p plen %d arg %p len %d val_size %d\n",
	           __FUNCTION__, actionid, name, params, plen, arg, len, val_size));

	if ((bcmerror = bcm_iovar_lencheck(vi, arg, len, IOV_ISSET(actionid))) != 0)
		goto exit;

	if (plen >= (int)sizeof(int_val))
		bcopy(params, &int_val, sizeof(int_val));

	if (plen >= (int)sizeof(int_val) * 2)
		bcopy((void*)((uintptr)params + sizeof(int_val)), &int_val2, sizeof(int_val2));

	if (plen >= (int)sizeof(int_val) * 3)
		bcopy((void*)((uintptr)params + 2 * sizeof(int_val)), &int_val3, sizeof(int_val3));

	bool_val = (int_val != 0) ? TRUE : FALSE;

	/* Check if dongle is in reset. If so, only allow DEVRESET iovars */
	if (bus->dhd->dongle_reset && !(actionid == IOV_SVAL(IOV_DEVRESET) ||
	                                actionid == IOV_GVAL(IOV_DEVRESET))) {
		bcmerror = BCME_NOTREADY;
		goto exit;
	}

	switch (actionid) {

	case IOV_SVAL(IOV_VARS):
		bcmerror = dhdpcie_downloadvars(bus, arg, len);
		break;
#ifdef DHD_PCIE_REG_ACCESS
	case IOV_SVAL(IOV_PCIEREG):
		si_corereg(bus->sih, bus->sih->buscoreidx, OFFSETOF(sbpcieregs_t, configaddr), ~0,
			int_val);
		si_corereg(bus->sih, bus->sih->buscoreidx, OFFSETOF(sbpcieregs_t, configdata), ~0,
			int_val2);
		break;

	case IOV_GVAL(IOV_PCIEREG):
		si_corereg(bus->sih, bus->sih->buscoreidx, OFFSETOF(sbpcieregs_t, configaddr), ~0,
			int_val);
		int_val = si_corereg(bus->sih, bus->sih->buscoreidx,
			OFFSETOF(sbpcieregs_t, configdata), 0, 0);
		bcopy(&int_val, arg, sizeof(int_val));
		break;

	case IOV_SVAL(IOV_PCIECOREREG):
		si_corereg(bus->sih, bus->sih->buscoreidx, int_val, ~0, int_val2);
		break;
	case IOV_GVAL(IOV_BAR0_SECWIN_REG):
	{
		sdreg_t sdreg;
		uint32 addr, size;

		bcopy(params, &sdreg, sizeof(sdreg));

		addr = sdreg.offset;
		size = sdreg.func;

		if (serialized_backplane_access(bus, addr, size, (uint *)&int_val, TRUE) != BCME_OK)
		{
			DHD_ERROR(("Invalid size/addr combination \n"));
			bcmerror = BCME_ERROR;
			break;
		}
		bcopy(&int_val, arg, sizeof(int32));
		break;
	}

	case IOV_SVAL(IOV_BAR0_SECWIN_REG):
	{
		sdreg_t sdreg;
		uint32 addr, size;

		bcopy(params, &sdreg, sizeof(sdreg));

		addr = sdreg.offset;
		size = sdreg.func;
		if (serialized_backplane_access(bus, addr, size,
			(uint *)(&sdreg.value), FALSE) != BCME_OK) {
			DHD_ERROR(("Invalid size/addr combination \n"));
			bcmerror = BCME_ERROR;
		}
		break;
	}

	case IOV_GVAL(IOV_SBREG):
	{
		sdreg_t sdreg;
		uint32 addr, size;

		bcopy(params, &sdreg, sizeof(sdreg));

		addr = sdreg.offset | SI_ENUM_BASE(bus->sih);
		size = sdreg.func;

		if (serialized_backplane_access(bus, addr, size, (uint *)&int_val, TRUE) != BCME_OK)
		{
			DHD_ERROR(("Invalid size/addr combination \n"));
			bcmerror = BCME_ERROR;
			break;
		}
		bcopy(&int_val, arg, size);
		break;
	}

	case IOV_SVAL(IOV_SBREG):
	{
		sdreg_t sdreg;
		uint32 addr, size;

		bcopy(params, &sdreg, sizeof(sdreg));

		addr = sdreg.offset | SI_ENUM_BASE(bus->sih);
		size = sdreg.func;
		if (serialized_backplane_access(bus, addr, size,
			(uint *)(&sdreg.value), FALSE) != BCME_OK) {
			DHD_ERROR(("Invalid size/addr combination \n"));
			bcmerror = BCME_ERROR;
		}
		break;
	}

	case IOV_GVAL(IOV_PCIESERDESREG):
	{
		uint val;
		if (!PCIE_GEN2(bus->sih)) {
			DHD_ERROR(("supported only in pcie gen2\n"));
			bcmerror = BCME_ERROR;
			break;
		}

		if (!pcie2_mdioop(bus, int_val, int_val2, FALSE, &val, FALSE)) {
			bcopy(&val, arg, sizeof(int32));
		} else {
			DHD_ERROR(("pcie2_mdioop failed.\n"));
			bcmerror = BCME_ERROR;
		}
		break;
	}

	case IOV_SVAL(IOV_PCIESERDESREG):
		if (!PCIE_GEN2(bus->sih)) {
			DHD_ERROR(("supported only in pcie gen2\n"));
			bcmerror = BCME_ERROR;
			break;
		}
		if (pcie2_mdioop(bus, int_val, int_val2, TRUE, (uint *)&int_val3, FALSE)) {
			DHD_ERROR(("pcie2_mdioop failed.\n"));
			bcmerror = BCME_ERROR;
		}
		break;
	case IOV_GVAL(IOV_PCIECOREREG):
		int_val = si_corereg(bus->sih, bus->sih->buscoreidx, int_val, 0, 0);
		bcopy(&int_val, arg, sizeof(int_val));
		break;

	case IOV_SVAL(IOV_PCIECFGREG):
		OSL_PCI_WRITE_CONFIG(bus->osh, int_val, 4, int_val2);
		break;

	case IOV_GVAL(IOV_PCIECFGREG):
		int_val = OSL_PCI_READ_CONFIG(bus->osh, int_val, 4);
		bcopy(&int_val, arg, sizeof(int_val));
		break;
#endif /* DHD_PCIE_REG_ACCESS */
	case IOV_SVAL(IOV_PCIE_LPBK):
		bcmerror = dhdpcie_bus_lpback_req(bus, int_val);
		break;

	case IOV_SVAL(IOV_PCIE_DMAXFER): {
		dma_xfer_info_t *dmaxfer = (dma_xfer_info_t *)arg;

		if (!dmaxfer)
			return BCME_BADARG;
		if (dmaxfer->version != DHD_DMAXFER_VERSION)
			return BCME_VERSION;
		if (dmaxfer->length != sizeof(dma_xfer_info_t)) {
			return BCME_BADLEN;
		}

		bcmerror = dhdpcie_bus_dmaxfer_req(bus, dmaxfer->num_bytes,
				dmaxfer->src_delay, dmaxfer->dest_delay,
				dmaxfer->type, dmaxfer->core_num,
				dmaxfer->should_wait);

		if (dmaxfer->should_wait && bcmerror >= 0) {
			bcmerror = dhdmsgbuf_dmaxfer_status(bus->dhd, dmaxfer);
		}
		break;
	}

	case IOV_GVAL(IOV_PCIE_DMAXFER): {
		dma_xfer_info_t *dmaxfer = (dma_xfer_info_t *)params;
		if (!dmaxfer)
			return BCME_BADARG;
		if (dmaxfer->version != DHD_DMAXFER_VERSION)
			return BCME_VERSION;
		if (dmaxfer->length != sizeof(dma_xfer_info_t)) {
			return BCME_BADLEN;
		}
		bcmerror = dhdmsgbuf_dmaxfer_status(bus->dhd, dmaxfer);
		break;
	}

	case IOV_GVAL(IOV_PCIE_SUSPEND):
		int_val = (bus->dhd->busstate == DHD_BUS_SUSPEND) ? 1 : 0;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_PCIE_SUSPEND):
		if (bool_val) { /* Suspend */
			int ret;
			unsigned long flags;

			/*
			 * If some other context is busy, wait until they are done,
			 * before starting suspend
			 */
			ret = dhd_os_busbusy_wait_condition(bus->dhd,
				&bus->dhd->dhd_bus_busy_state, DHD_BUS_BUSY_IN_DHD_IOVAR);
			if (ret == 0) {
				DHD_ERROR(("%s:Wait Timedout, dhd_bus_busy_state = 0x%x\n",
					__FUNCTION__, bus->dhd->dhd_bus_busy_state));
				return BCME_BUSY;
			}

			DHD_GENERAL_LOCK(bus->dhd, flags);
			DHD_BUS_BUSY_SET_SUSPEND_IN_PROGRESS(bus->dhd);
			DHD_GENERAL_UNLOCK(bus->dhd, flags);
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
			dhdpcie_bus_suspend(bus, TRUE, TRUE);
#else
			dhdpcie_bus_suspend(bus, TRUE);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

			DHD_GENERAL_LOCK(bus->dhd, flags);
			DHD_BUS_BUSY_CLEAR_SUSPEND_IN_PROGRESS(bus->dhd);
			dhd_os_busbusy_wake(bus->dhd);
			DHD_GENERAL_UNLOCK(bus->dhd, flags);
		} else { /* Resume */
			unsigned long flags;
			DHD_GENERAL_LOCK(bus->dhd, flags);
			DHD_BUS_BUSY_SET_RESUME_IN_PROGRESS(bus->dhd);
			DHD_GENERAL_UNLOCK(bus->dhd, flags);

			dhdpcie_bus_suspend(bus, FALSE);

			DHD_GENERAL_LOCK(bus->dhd, flags);
			DHD_BUS_BUSY_CLEAR_RESUME_IN_PROGRESS(bus->dhd);
			dhd_os_busbusy_wake(bus->dhd);
			DHD_GENERAL_UNLOCK(bus->dhd, flags);
		}
		break;

	case IOV_GVAL(IOV_MEMSIZE):
		int_val = (int32)bus->ramsize;
		bcopy(&int_val, arg, val_size);
		break;
#ifdef DHD_BUS_MEM_ACCESS
	case IOV_SVAL(IOV_MEMBYTES):
	case IOV_GVAL(IOV_MEMBYTES):
	{
		uint32 address;		/* absolute backplane address */
		uint size, dsize;
		uint8 *data;

		bool set = (actionid == IOV_SVAL(IOV_MEMBYTES));

		ASSERT(plen >= 2*sizeof(int));

		address = (uint32)int_val;
		bcopy((char *)params + sizeof(int_val), &int_val, sizeof(int_val));
		size = (uint)int_val;

		/* Do some validation */
		dsize = set ? plen - (2 * sizeof(int)) : len;
		if (dsize < size) {
			DHD_ERROR(("%s: error on %s membytes, addr 0x%08x size %d dsize %d\n",
			           __FUNCTION__, (set ? "set" : "get"), address, size, dsize));
			bcmerror = BCME_BADARG;
			break;
		}

		DHD_INFO(("%s: Request to %s %d bytes at address 0x%08x\n dsize %d ", __FUNCTION__,
		          (set ? "write" : "read"), size, address, dsize));

		/* check if CR4 */
		if (si_setcore(bus->sih, ARMCR4_CORE_ID, 0) ||
		    si_setcore(bus->sih, SYSMEM_CORE_ID, 0)) {
			/* if address is 0, store the reset instruction to be written in 0 */
			if (set && address == bus->dongle_ram_base) {
				bus->resetinstr = *(((uint32*)params) + 2);
			}
		} else {
		/* If we know about SOCRAM, check for a fit */
		if ((bus->orig_ramsize) &&
		    ((address > bus->orig_ramsize) || (address + size > bus->orig_ramsize)))
		{
			uint8 enable, protect, remap;
			si_socdevram(bus->sih, FALSE, &enable, &protect, &remap);
			if (!enable || protect) {
				DHD_ERROR(("%s: ramsize 0x%08x doesn't have %d bytes at 0x%08x\n",
					__FUNCTION__, bus->orig_ramsize, size, address));
				DHD_ERROR(("%s: socram enable %d, protect %d\n",
					__FUNCTION__, enable, protect));
				bcmerror = BCME_BADARG;
				break;
			}

			if (!REMAP_ENAB(bus) && (address >= SOCDEVRAM_ARM_ADDR)) {
				uint32 devramsize = si_socdevram_size(bus->sih);
				if ((address < SOCDEVRAM_ARM_ADDR) ||
					(address + size > (SOCDEVRAM_ARM_ADDR + devramsize))) {
					DHD_ERROR(("%s: bad address 0x%08x, size 0x%08x\n",
						__FUNCTION__, address, size));
					DHD_ERROR(("%s: socram range 0x%08x,size 0x%08x\n",
						__FUNCTION__, SOCDEVRAM_ARM_ADDR, devramsize));
					bcmerror = BCME_BADARG;
					break;
				}
				/* move it such that address is real now */
				address -= SOCDEVRAM_ARM_ADDR;
				address += SOCDEVRAM_BP_ADDR;
				DHD_INFO(("%s: Request to %s %d bytes @ Mapped address 0x%08x\n",
					__FUNCTION__, (set ? "write" : "read"), size, address));
			} else if (REMAP_ENAB(bus) && REMAP_ISADDR(bus, address) && remap) {
				/* Can not access remap region while devram remap bit is set
				 * ROM content would be returned in this case
				 */
				DHD_ERROR(("%s: Need to disable remap for address 0x%08x\n",
					__FUNCTION__, address));
				bcmerror = BCME_ERROR;
				break;
			}
		}
		}

		/* Generate the actual data pointer */
		data = set ? (uint8*)params + 2 * sizeof(int): (uint8*)arg;

		/* Call to do the transfer */
		bcmerror = dhdpcie_bus_membytes(bus, set, address, data, size);

		break;
	}
#endif /* DHD_BUS_MEM_ACCESS */

	/* Debug related. Dumps core registers or one of the dongle memory */
	case IOV_GVAL(IOV_DUMP_DONGLE):
	{
		dump_dongle_in_t ddi = *(dump_dongle_in_t*)params;
		dump_dongle_out_t *ddo = (dump_dongle_out_t*)arg;
		uint32 *p = ddo->val;
		const uint max_offset = 4096 - 1; /* one core contains max 4096/4 registers */

		if (plen < sizeof(ddi) || len < sizeof(ddo)) {
			bcmerror = BCME_BADARG;
			break;
		}

		switch (ddi.type) {
		case DUMP_DONGLE_COREREG:
			ddo->n_bytes = 0;

			if (si_setcoreidx(bus->sih, ddi.index) == NULL) {
				break; // beyond last core: core enumeration ended
			}

			ddo->address = si_addrspace(bus->sih, CORE_SLAVE_PORT_0, CORE_BASE_ADDR_0);
			ddo->address += ddi.offset; // BP address at which this dump starts

			ddo->id = si_coreid(bus->sih);
			ddo->rev = si_corerev(bus->sih);

			while (ddi.offset < max_offset &&
				sizeof(dump_dongle_out_t) + ddo->n_bytes < (uint)len) {
				*p++ = si_corereg(bus->sih, ddi.index, ddi.offset, 0, 0);
				ddi.offset += sizeof(uint32);
				ddo->n_bytes += sizeof(uint32);
			}
			break;
		default:
			// TODO: implement d11 SHM/TPL dumping
			bcmerror = BCME_BADARG;
			break;
		}
		break;
	}

	/* Debug related. Returns a string with dongle capabilities */
	case IOV_GVAL(IOV_DNGL_CAPS):
	{
		strncpy(arg, bus->dhd->fw_capabilities,
			MIN(strlen(bus->dhd->fw_capabilities), (size_t)len));
		((char*)arg)[len - 1] = '\0';
		break;
	}

#if defined(DEBUGGER) || defined(DHD_DSCOPE)
	case IOV_SVAL(IOV_GDB_SERVER):
		/* debugger_*() functions may sleep, so cannot hold spinlock */
		DHD_PERIM_UNLOCK(bus->dhd);
		if (int_val > 0) {
			debugger_init((void *) bus, &bus_ops, int_val, SI_ENUM_BASE(bus->sih));
		} else {
			debugger_close();
		}
		DHD_PERIM_LOCK(bus->dhd);
		break;
#endif /* DEBUGGER || DHD_DSCOPE */

#ifdef BCM_BUZZZ
	/* Dump dongle side buzzz trace to console */
	case IOV_GVAL(IOV_BUZZZ_DUMP):
		bcmerror = dhd_buzzz_dump_dngl(bus);
		break;
#endif /* BCM_BUZZZ */

	case IOV_SVAL(IOV_SET_DOWNLOAD_STATE):
		bcmerror = dhdpcie_bus_download_state(bus, bool_val);
		break;

	case IOV_GVAL(IOV_RAMSIZE):
		int_val = (int32)bus->ramsize;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_RAMSIZE):
		bus->ramsize = int_val;
		bus->orig_ramsize = int_val;
		break;

	case IOV_GVAL(IOV_RAMSTART):
		int_val = (int32)bus->dongle_ram_base;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_CC_NVMSHADOW):
	{
		struct bcmstrbuf dump_b;

		bcm_binit(&dump_b, arg, len);
		bcmerror = dhdpcie_cc_nvmshadow(bus, &dump_b);
		break;
	}

	case IOV_GVAL(IOV_SLEEP_ALLOWED):
		bool_val = bus->sleep_allowed;
		bcopy(&bool_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SLEEP_ALLOWED):
		bus->sleep_allowed = bool_val;
		break;

	case IOV_GVAL(IOV_DONGLEISOLATION):
		int_val = bus->dhd->dongle_isolation;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DONGLEISOLATION):
		bus->dhd->dongle_isolation = bool_val;
		break;

	case IOV_GVAL(IOV_LTRSLEEPON_UNLOOAD):
		int_val = bus->ltrsleep_on_unload;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_LTRSLEEPON_UNLOOAD):
		bus->ltrsleep_on_unload = bool_val;
		break;

	case IOV_GVAL(IOV_DUMP_RINGUPD_BLOCK):
	{
		struct bcmstrbuf dump_b;
		bcm_binit(&dump_b, arg, len);
		bcmerror = dhd_prot_ringupd_dump(bus->dhd, &dump_b);
		break;
	}
	case IOV_GVAL(IOV_DMA_RINGINDICES):
	{
		int_val = dhdpcie_get_dma_ring_indices(bus->dhd);
		bcopy(&int_val, arg, sizeof(int_val));
		break;
	}
	case IOV_SVAL(IOV_DMA_RINGINDICES):
		bcmerror = dhdpcie_set_dma_ring_indices(bus->dhd, int_val);
		break;

	case IOV_GVAL(IOV_METADATA_DBG):
		int_val = dhd_prot_metadata_dbg_get(bus->dhd);
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_METADATA_DBG):
		dhd_prot_metadata_dbg_set(bus->dhd, (int_val != 0));
		break;

	case IOV_GVAL(IOV_RX_METADATALEN):
		int_val = dhd_prot_metadatalen_get(bus->dhd, TRUE);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_RX_METADATALEN):
		if (int_val > 64) {
			bcmerror = BCME_BUFTOOLONG;
			break;
		}
		dhd_prot_metadatalen_set(bus->dhd, int_val, TRUE);
		break;

	case IOV_SVAL(IOV_TXP_THRESHOLD):
		dhd_prot_txp_threshold(bus->dhd, TRUE, int_val);
		break;

	case IOV_GVAL(IOV_TXP_THRESHOLD):
		int_val = dhd_prot_txp_threshold(bus->dhd, FALSE, int_val);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DB1_FOR_MB):
		if (int_val)
			bus->db1_for_mb = TRUE;
		else
			bus->db1_for_mb = FALSE;
		break;

	case IOV_GVAL(IOV_DB1_FOR_MB):
		if (bus->db1_for_mb)
			int_val = 1;
		else
			int_val = 0;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_TX_METADATALEN):
		int_val = dhd_prot_metadatalen_get(bus->dhd, FALSE);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_TX_METADATALEN):
		if (int_val > 64) {
			bcmerror = BCME_BUFTOOLONG;
			break;
		}
		dhd_prot_metadatalen_set(bus->dhd, int_val, FALSE);
		break;

	case IOV_SVAL(IOV_DEVRESET):
		switch (int_val) {
			case DHD_BUS_DEVRESET_ON:
				bcmerror = dhd_bus_devreset(bus->dhd, (uint8)int_val);
				break;
			case DHD_BUS_DEVRESET_OFF:
				bcmerror = dhd_bus_devreset(bus->dhd, (uint8)int_val);
				break;
			case DHD_BUS_DEVRESET_FLR:
				bcmerror = dhd_bus_perform_flr(bus, bus->flr_force_fail);
				break;
			case DHD_BUS_DEVRESET_FLR_FORCE_FAIL:
				bus->flr_force_fail = TRUE;
				break;
			default:
				DHD_ERROR(("%s: invalid argument for devreset\n", __FUNCTION__));
				break;
		}
		break;
	case IOV_SVAL(IOV_FORCE_FW_TRAP):
		if (bus->dhd->busstate == DHD_BUS_DATA)
			dhdpcie_fw_trap(bus);
		else {
			DHD_ERROR(("%s: Bus is NOT up\n", __FUNCTION__));
			bcmerror = BCME_NOTUP;
		}
		break;
	case IOV_GVAL(IOV_FLOW_PRIO_MAP):
		int_val = bus->dhd->flow_prio_map_type;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_FLOW_PRIO_MAP):
		int_val = (int32)dhd_update_flow_prio_map(bus->dhd, (uint8)int_val);
		bcopy(&int_val, arg, val_size);
		break;

#ifdef DHD_PCIE_RUNTIMEPM
	case IOV_GVAL(IOV_IDLETIME):
		if (!(bus->dhd->op_mode & DHD_FLAG_MFG_MODE)) {
			int_val = bus->idletime;
		} else {
			int_val = 0;
		}
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_IDLETIME):
		if (int_val < 0) {
			bcmerror = BCME_BADARG;
		} else {
			bus->idletime = int_val;
			if (bus->idletime) {
				DHD_ENABLE_RUNTIME_PM(bus->dhd);
			} else {
				DHD_DISABLE_RUNTIME_PM(bus->dhd);
			}
		}
		break;
#endif /* DHD_PCIE_RUNTIMEPM */

	case IOV_GVAL(IOV_TXBOUND):
		int_val = (int32)dhd_txbound;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_TXBOUND):
		dhd_txbound = (uint)int_val;
		break;

	case IOV_SVAL(IOV_H2D_MAILBOXDATA):
		dhdpcie_send_mb_data(bus, (uint)int_val);
		break;

	case IOV_SVAL(IOV_INFORINGS):
		dhd_prot_init_info_rings(bus->dhd);
		break;

	case IOV_SVAL(IOV_H2D_PHASE):
		if (bus->dhd->busstate != DHD_BUS_DOWN) {
			DHD_ERROR(("%s: Can change only when bus down (before FW download)\n",
				__FUNCTION__));
			bcmerror = BCME_NOTDOWN;
			break;
		}
		if (int_val)
			bus->dhd->h2d_phase_supported = TRUE;
		else
			bus->dhd->h2d_phase_supported = FALSE;
		break;

	case IOV_GVAL(IOV_H2D_PHASE):
		int_val = (int32) bus->dhd->h2d_phase_supported;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_H2D_ENABLE_TRAP_BADPHASE):
		if (bus->dhd->busstate != DHD_BUS_DOWN) {
			DHD_ERROR(("%s: Can change only when bus down (before FW download)\n",
				__FUNCTION__));
			bcmerror = BCME_NOTDOWN;
			break;
		}
		if (int_val)
			bus->dhd->force_dongletrap_on_bad_h2d_phase = TRUE;
		else
			bus->dhd->force_dongletrap_on_bad_h2d_phase = FALSE;
		break;

	case IOV_GVAL(IOV_H2D_ENABLE_TRAP_BADPHASE):
		int_val = (int32) bus->dhd->force_dongletrap_on_bad_h2d_phase;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_H2D_TXPOST_MAX_ITEM):
		if (bus->dhd->busstate != DHD_BUS_DOWN) {
			DHD_ERROR(("%s: Can change only when bus down (before FW download)\n",
				__FUNCTION__));
			bcmerror = BCME_NOTDOWN;
			break;
		}
		dhd_prot_set_h2d_max_txpost(bus->dhd, (uint16)int_val);
		break;

	case IOV_GVAL(IOV_H2D_TXPOST_MAX_ITEM):
		int_val = dhd_prot_get_h2d_max_txpost(bus->dhd);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_RXBOUND):
		int_val = (int32)dhd_rxbound;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_RXBOUND):
		dhd_rxbound = (uint)int_val;
		break;

	case IOV_GVAL(IOV_TRAPDATA):
	{
		struct bcmstrbuf dump_b;
		bcm_binit(&dump_b, arg, len);
		bcmerror = dhd_prot_dump_extended_trap(bus->dhd, &dump_b, FALSE);
		break;
	}

	case IOV_GVAL(IOV_TRAPDATA_RAW):
	{
		struct bcmstrbuf dump_b;
		bcm_binit(&dump_b, arg, len);
		bcmerror = dhd_prot_dump_extended_trap(bus->dhd, &dump_b, TRUE);
		break;
	}
#ifdef DHD_PCIE_REG_ACCESS
	case IOV_GVAL(IOV_PCIEASPM): {
		uint8 clkreq = 0;
		uint32 aspm = 0;

		/* this command is to hide the details, but match the lcreg
		#define PCIE_CLKREQ_ENAB		0x100
		#define PCIE_ASPM_L1_ENAB        	2
		#define PCIE_ASPM_L0s_ENAB       	1
		*/

		clkreq = dhdpcie_clkreq(bus->dhd->osh, 0, 0);
		aspm = dhdpcie_lcreg(bus->dhd->osh, 0, 0);

		int_val = ((clkreq & 0x1) << 8) | (aspm & PCIE_ASPM_ENAB);
		bcopy(&int_val, arg, val_size);
		break;
	}

	case IOV_SVAL(IOV_PCIEASPM): {
		uint32 tmp;

		tmp = dhdpcie_lcreg(bus->dhd->osh, 0, 0);
		dhdpcie_lcreg(bus->dhd->osh, PCIE_ASPM_ENAB,
			(tmp & ~PCIE_ASPM_ENAB) | (int_val & PCIE_ASPM_ENAB));

		dhdpcie_clkreq(bus->dhd->osh, 1, ((int_val & 0x100) >> 8));
		break;
	}
#endif /* DHD_PCIE_REG_ACCESS */
	case IOV_SVAL(IOV_HANGREPORT):
		bus->dhd->hang_report = bool_val;
		DHD_ERROR(("%s: Set hang_report as %d\n",
			__FUNCTION__, bus->dhd->hang_report));
		break;

	case IOV_GVAL(IOV_HANGREPORT):
		int_val = (int32)bus->dhd->hang_report;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_CTO_PREVENTION):
		bcmerror = dhdpcie_cto_init(bus, bool_val);
		break;

	case IOV_GVAL(IOV_CTO_PREVENTION):
		if (bus->sih->buscorerev < 19) {
			bcmerror = BCME_UNSUPPORTED;
			break;
		}
		int_val = (int32)bus->cto_enable;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_CTO_THRESHOLD):
		{
			if (bus->sih->buscorerev < 19) {
				bcmerror = BCME_UNSUPPORTED;
				break;
			}
			bus->cto_threshold = (uint32)int_val;
		}
		break;

	case IOV_GVAL(IOV_CTO_THRESHOLD):
		if (bus->sih->buscorerev < 19) {
			bcmerror = BCME_UNSUPPORTED;
			break;
		}
		if (bus->cto_threshold)
			int_val = (int32)bus->cto_threshold;
		else
			int_val = (int32)PCIE_CTO_TO_THRESH_DEFAULT;

		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_PCIE_WD_RESET):
		if (bool_val) {
			/* Legacy chipcommon watchdog reset */
			dhdpcie_cc_watchdog_reset(bus);
		}
		break;

	case IOV_GVAL(IOV_HWA_ENAB_BMAP):
		int_val = bus->hwa_enab_bmap;
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_HWA_ENAB_BMAP):
		bus->hwa_enab_bmap = (uint8)int_val;
		break;
	case IOV_GVAL(IOV_IDMA_ENABLE):
		int_val = bus->idma_enabled;
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_IDMA_ENABLE):
		bus->idma_enabled = (bool)int_val;
		break;
	case IOV_GVAL(IOV_IFRM_ENABLE):
		int_val = bus->ifrm_enabled;
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_IFRM_ENABLE):
		bus->ifrm_enabled = (bool)int_val;
		break;
	case IOV_GVAL(IOV_CLEAR_RING):
		bcopy(&int_val, arg, val_size);
		dhd_flow_rings_flush(bus->dhd, 0);
		break;
	case IOV_GVAL(IOV_DAR_ENABLE):
		int_val = bus->dar_enabled;
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_DAR_ENABLE):
		bus->dar_enabled = (bool)int_val;
		break;
	case IOV_GVAL(IOV_HSCBSIZE):
		bcmerror = dhd_get_hscb_info(bus->dhd, NULL, (uint32 *)arg);
		break;
#ifdef DHD_BUS_MEM_ACCESS
	case IOV_GVAL(IOV_HSCBBYTES):
		bcmerror = dhd_get_hscb_buff(bus->dhd, int_val, int_val2, (void*)arg);
		break;
#endif // endif

#ifdef DHD_HP2P
	case IOV_SVAL(IOV_HP2P_ENABLE):
		dhd_prot_hp2p_enable(bus->dhd, TRUE, int_val);
		break;

	case IOV_GVAL(IOV_HP2P_ENABLE):
		int_val = dhd_prot_hp2p_enable(bus->dhd, FALSE, int_val);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_HP2P_PKT_THRESHOLD):
		dhd_prot_pkt_threshold(bus->dhd, TRUE, int_val);
		break;

	case IOV_GVAL(IOV_HP2P_PKT_THRESHOLD):
		int_val = dhd_prot_pkt_threshold(bus->dhd, FALSE, int_val);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_HP2P_TIME_THRESHOLD):
		dhd_prot_time_threshold(bus->dhd, TRUE, int_val);
		break;

	case IOV_GVAL(IOV_HP2P_TIME_THRESHOLD):
		int_val = dhd_prot_time_threshold(bus->dhd, FALSE, int_val);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_HP2P_PKT_EXPIRY):
		dhd_prot_pkt_expiry(bus->dhd, TRUE, int_val);
		break;

	case IOV_GVAL(IOV_HP2P_PKT_EXPIRY):
		int_val = dhd_prot_pkt_expiry(bus->dhd, FALSE, int_val);
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_HP2P_TXCPL_MAXITEMS):
		if (bus->dhd->busstate != DHD_BUS_DOWN) {
			return BCME_NOTDOWN;
		}
		dhd_bus_set_hp2p_ring_max_size(bus, TRUE, int_val);
		break;

	case IOV_GVAL(IOV_HP2P_TXCPL_MAXITEMS):
		int_val = dhd_bus_get_hp2p_ring_max_size(bus, TRUE);
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_HP2P_RXCPL_MAXITEMS):
		if (bus->dhd->busstate != DHD_BUS_DOWN) {
			return BCME_NOTDOWN;
		}
		dhd_bus_set_hp2p_ring_max_size(bus, FALSE, int_val);
		break;

	case IOV_GVAL(IOV_HP2P_RXCPL_MAXITEMS):
		int_val = dhd_bus_get_hp2p_ring_max_size(bus, FALSE);
		bcopy(&int_val, arg, val_size);
		break;
#endif /* DHD_HP2P */
	case IOV_SVAL(IOV_EXTDTXS_IN_TXCPL):
		if (bus->dhd->busstate != DHD_BUS_DOWN) {
			return BCME_NOTDOWN;
		}
		if (int_val)
			bus->dhd->extdtxs_in_txcpl = TRUE;
		else
			bus->dhd->extdtxs_in_txcpl = FALSE;
		break;

	case IOV_GVAL(IOV_EXTDTXS_IN_TXCPL):
		int_val = bus->dhd->extdtxs_in_txcpl;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_HOSTRDY_AFTER_INIT):
		if (bus->dhd->busstate != DHD_BUS_DOWN) {
			return BCME_NOTDOWN;
		}
		if (int_val)
			bus->dhd->hostrdy_after_init = TRUE;
		else
			bus->dhd->hostrdy_after_init = FALSE;
		break;

	case IOV_GVAL(IOV_HOSTRDY_AFTER_INIT):
		int_val = bus->dhd->hostrdy_after_init;
		bcopy(&int_val, arg, val_size);
		break;

	default:
		bcmerror = BCME_UNSUPPORTED;
		break;
	}

exit:
	return bcmerror;
} /* dhdpcie_bus_doiovar */

/** Transfers bytes from host to dongle using pio mode */
static int
dhdpcie_bus_lpback_req(struct  dhd_bus *bus, uint32 len)
{
	if (bus->dhd == NULL) {
		DHD_ERROR(("bus not inited\n"));
		return 0;
	}
	if (bus->dhd->prot == NULL) {
		DHD_ERROR(("prot is not inited\n"));
		return 0;
	}
	if (bus->dhd->busstate != DHD_BUS_DATA) {
		DHD_ERROR(("not in a readystate to LPBK  is not inited\n"));
		return 0;
	}
	dhdmsgbuf_lpbk_req(bus->dhd, len);
	return 0;
}

void
dhd_bus_dump_dar_registers(struct dhd_bus *bus)
{
	uint32 dar_clk_ctrl_val, dar_pwr_ctrl_val, dar_intstat_val,
		dar_errlog_val, dar_erraddr_val, dar_pcie_mbint_val;
	uint32 dar_clk_ctrl_reg, dar_pwr_ctrl_reg, dar_intstat_reg,
		dar_errlog_reg, dar_erraddr_reg, dar_pcie_mbint_reg;

	if (bus->is_linkdown && !bus->cto_triggered) {
		DHD_ERROR(("%s: link is down\n", __FUNCTION__));
		return;
	}

	dar_clk_ctrl_reg = (uint32)DAR_CLK_CTRL(bus->sih->buscorerev);
	dar_pwr_ctrl_reg = (uint32)DAR_PCIE_PWR_CTRL(bus->sih->buscorerev);
	dar_intstat_reg = (uint32)DAR_INTSTAT(bus->sih->buscorerev);
	dar_errlog_reg = (uint32)DAR_ERRLOG(bus->sih->buscorerev);
	dar_erraddr_reg = (uint32)DAR_ERRADDR(bus->sih->buscorerev);
	dar_pcie_mbint_reg = (uint32)DAR_PCIMailBoxInt(bus->sih->buscorerev);

	if (bus->sih->buscorerev < 24) {
		DHD_ERROR(("%s: DAR not supported for corerev(%d) < 24\n",
			__FUNCTION__, bus->sih->buscorerev));
		return;
	}

	dar_clk_ctrl_val = si_corereg(bus->sih, bus->sih->buscoreidx, dar_clk_ctrl_reg, 0, 0);
	dar_pwr_ctrl_val = si_corereg(bus->sih, bus->sih->buscoreidx, dar_pwr_ctrl_reg, 0, 0);
	dar_intstat_val = si_corereg(bus->sih, bus->sih->buscoreidx, dar_intstat_reg, 0, 0);
	dar_errlog_val = si_corereg(bus->sih, bus->sih->buscoreidx, dar_errlog_reg, 0, 0);
	dar_erraddr_val = si_corereg(bus->sih, bus->sih->buscoreidx, dar_erraddr_reg, 0, 0);
	dar_pcie_mbint_val = si_corereg(bus->sih, bus->sih->buscoreidx, dar_pcie_mbint_reg, 0, 0);

	DHD_ERROR(("%s: dar_clk_ctrl(0x%x:0x%x) dar_pwr_ctrl(0x%x:0x%x) dar_intstat(0x%x:0x%x)\n",
		__FUNCTION__, dar_clk_ctrl_reg, dar_clk_ctrl_val,
		dar_pwr_ctrl_reg, dar_pwr_ctrl_val, dar_intstat_reg, dar_intstat_val));

	DHD_ERROR(("%s: dar_errlog(0x%x:0x%x) dar_erraddr(0x%x:0x%x) dar_pcie_mbint(0x%x:0x%x)\n",
		__FUNCTION__, dar_errlog_reg, dar_errlog_val,
		dar_erraddr_reg, dar_erraddr_val, dar_pcie_mbint_reg, dar_pcie_mbint_val));
}

/* Ring DoorBell1 to indicate Hostready i.e. D3 Exit */
void
dhd_bus_hostready(struct  dhd_bus *bus)
{
	if (!bus->dhd->d2h_hostrdy_supported) {
		return;
	}

	if (bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link was down\n", __FUNCTION__));
		return;
	}

	DHD_ERROR(("%s : Read PCICMD Reg: 0x%08X\n", __FUNCTION__,
		dhd_pcie_config_read(bus->osh, PCI_CFG_CMD, sizeof(uint32))));

	if (DAR_PWRREQ(bus)) {
		dhd_bus_pcie_pwr_req(bus);
	}

	dhd_bus_dump_dar_registers(bus);

	si_corereg(bus->sih, bus->sih->buscoreidx, dhd_bus_db1_addr_get(bus), ~0, 0x12345678);
	bus->hostready_count ++;
	DHD_ERROR(("%s: Ring Hostready:%d\n", __FUNCTION__, bus->hostready_count));
}

/* Clear INTSTATUS */
void
dhdpcie_bus_clear_intstatus(struct dhd_bus *bus)
{
	uint32 intstatus = 0;
	if ((bus->sih->buscorerev == 6) || (bus->sih->buscorerev == 4) ||
		(bus->sih->buscorerev == 2)) {
		intstatus = dhdpcie_bus_cfg_read_dword(bus, PCIIntstatus, 4);
		dhdpcie_bus_cfg_write_dword(bus, PCIIntstatus, 4, intstatus);
	} else {
		/* this is a PCIE core register..not a config register... */
		intstatus = si_corereg(bus->sih, bus->sih->buscoreidx, bus->pcie_mailbox_int, 0, 0);
		si_corereg(bus->sih, bus->sih->buscoreidx, bus->pcie_mailbox_int, bus->def_intmask,
			intstatus);
	}
}

int
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
dhdpcie_bus_suspend(struct dhd_bus *bus, bool state, bool byint)
#else
dhdpcie_bus_suspend(struct dhd_bus *bus, bool state)
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
{
	int timeleft;
	int rc = 0;
	unsigned long flags, flags_bus;
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	int d3_read_retry = 0;
	uint32 d2h_mb_data = 0;
	uint32 zero = 0;
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

	if (bus->dhd == NULL) {
		DHD_ERROR(("bus not inited\n"));
		return BCME_ERROR;
	}
	if (bus->dhd->prot == NULL) {
		DHD_ERROR(("prot is not inited\n"));
		return BCME_ERROR;
	}

	if (dhd_query_bus_erros(bus->dhd)) {
		return BCME_ERROR;
	}

	DHD_GENERAL_LOCK(bus->dhd, flags);
	if (!(bus->dhd->busstate == DHD_BUS_DATA || bus->dhd->busstate == DHD_BUS_SUSPEND)) {
		DHD_ERROR(("not in a readystate\n"));
		DHD_GENERAL_UNLOCK(bus->dhd, flags);
		return BCME_ERROR;
	}
	DHD_GENERAL_UNLOCK(bus->dhd, flags);
	if (bus->dhd->dongle_reset) {
		DHD_ERROR(("Dongle is in reset state.\n"));
		return -EIO;
	}

	/* Check whether we are already in the requested state.
	 * state=TRUE means Suspend
	 * state=FALSE meanse Resume
	 */
	if (state == TRUE && bus->dhd->busstate == DHD_BUS_SUSPEND) {
		DHD_ERROR(("Bus is already in SUSPEND state.\n"));
		return BCME_OK;
	} else if (state == FALSE && bus->dhd->busstate == DHD_BUS_DATA) {
		DHD_ERROR(("Bus is already in RESUME state.\n"));
		return BCME_OK;
	}

	if (state) {
#ifdef OEM_ANDROID
		int idle_retry = 0;
		int active;
#endif /* OEM_ANDROID */

		if (bus->is_linkdown) {
			DHD_ERROR(("%s: PCIe link was down, state=%d\n",
				__FUNCTION__, state));
			return BCME_ERROR;
		}

		/* Suspend */
		DHD_ERROR(("%s: Entering suspend state\n", __FUNCTION__));

		bus->dhd->dhd_watchdog_ms_backup = dhd_watchdog_ms;
		if (bus->dhd->dhd_watchdog_ms_backup) {
			DHD_ERROR(("%s: Disabling wdtick before going to suspend\n",
				__FUNCTION__));
			dhd_os_wd_timer(bus->dhd, 0);
		}

		DHD_GENERAL_LOCK(bus->dhd, flags);
		if (DHD_BUS_BUSY_CHECK_IN_TX(bus->dhd)) {
			DHD_ERROR(("Tx Request is not ended\n"));
			bus->dhd->busstate = DHD_BUS_DATA;
			DHD_GENERAL_UNLOCK(bus->dhd, flags);
			return -EBUSY;
		}

		bus->last_suspend_start_time = OSL_LOCALTIME_NS();

		/* stop all interface network queue. */
		dhd_bus_stop_queue(bus);
		DHD_GENERAL_UNLOCK(bus->dhd, flags);

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
		if (byint) {
			DHD_OS_WAKE_LOCK_WAIVE(bus->dhd);
			/* Clear wait_for_d3_ack before sending D3_INFORM */
			bus->wait_for_d3_ack = 0;
			dhdpcie_send_mb_data(bus, H2D_HOST_D3_INFORM);

			timeleft = dhd_os_d3ack_wait(bus->dhd, &bus->wait_for_d3_ack);
			DHD_OS_WAKE_LOCK_RESTORE(bus->dhd);
		} else {
			/* Clear wait_for_d3_ack before sending D3_INFORM */
			bus->wait_for_d3_ack = 0;
			dhdpcie_send_mb_data(bus, H2D_HOST_D3_INFORM | H2D_HOST_ACK_NOINT);
			while (!bus->wait_for_d3_ack && d3_read_retry < MAX_D3_ACK_TIMEOUT) {
				dhdpcie_handle_mb_data(bus);
				usleep_range(1000, 1500);
				d3_read_retry++;
			}
		}
#else
		DHD_OS_WAKE_LOCK_WAIVE(bus->dhd);
		/* Clear wait_for_d3_ack before sending D3_INFORM */
		bus->wait_for_d3_ack = 0;
		/*
		 * Send H2D_HOST_D3_INFORM to dongle and mark bus->bus_low_power_state
		 * to DHD_BUS_D3_INFORM_SENT in dhd_prot_ring_write_complete_mbdata
		 * inside atomic context, so that no more DBs will be
		 * rung after sending D3_INFORM
		 */
		dhdpcie_send_mb_data(bus, H2D_HOST_D3_INFORM);

		/* Wait for D3 ACK for D3_ACK_RESP_TIMEOUT seconds */

		timeleft = dhd_os_d3ack_wait(bus->dhd, &bus->wait_for_d3_ack);

#ifdef DHD_RECOVER_TIMEOUT
		if (bus->wait_for_d3_ack == 0) {
			/* If wait_for_d3_ack was not updated because D2H MB was not received */
			uint32 intstatus = si_corereg(bus->sih, bus->sih->buscoreidx,
				bus->pcie_mailbox_int, 0, 0);
			int host_irq_disabled = dhdpcie_irq_disabled(bus);
			if ((intstatus) && (intstatus != (uint32)-1) &&
				(timeleft == 0) && (!dhd_query_bus_erros(bus->dhd))) {
				DHD_ERROR(("%s: D3 ACK trying again intstatus=%x"
					" host_irq_disabled=%d\n",
					__FUNCTION__, intstatus, host_irq_disabled));
				dhd_pcie_intr_count_dump(bus->dhd);
				dhd_print_tasklet_status(bus->dhd);
				if (bus->api.fw_rev >= PCIE_SHARED_VERSION_6 &&
					!bus->use_mailbox) {
					dhd_prot_process_ctrlbuf(bus->dhd);
				} else {
					dhdpcie_handle_mb_data(bus);
				}
				timeleft = dhd_os_d3ack_wait(bus->dhd, &bus->wait_for_d3_ack);
				/* Clear Interrupts */
				dhdpcie_bus_clear_intstatus(bus);
			}
		} /* bus->wait_for_d3_ack was 0 */
#endif /* DHD_RECOVER_TIMEOUT */

		DHD_OS_WAKE_LOCK_RESTORE(bus->dhd);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

#ifdef OEM_ANDROID
		/* To allow threads that got pre-empted to complete.
		 */
		while ((active = dhd_os_check_wakelock_all(bus->dhd)) &&
			(idle_retry < MAX_WKLK_IDLE_CHECK)) {
			OSL_SLEEP(1);
			idle_retry++;
		}
#endif /* OEM_ANDROID */

		if (bus->wait_for_d3_ack) {
			DHD_ERROR(("%s: Got D3 Ack \n", __FUNCTION__));
			/* Got D3 Ack. Suspend the bus */
#ifdef OEM_ANDROID
			if (active) {
				DHD_ERROR(("%s():Suspend failed because of wakelock"
					"restoring Dongle to D0\n", __FUNCTION__));

				if (bus->dhd->dhd_watchdog_ms_backup) {
					DHD_ERROR(("%s: Enabling wdtick due to wakelock active\n",
						__FUNCTION__));
					dhd_os_wd_timer(bus->dhd,
						bus->dhd->dhd_watchdog_ms_backup);
				}

				/*
				 * Dongle still thinks that it has to be in D3 state until
				 * it gets a D0 Inform, but we are backing off from suspend.
				 * Ensure that Dongle is brought back to D0.
				 *
				 * Bringing back Dongle from D3 Ack state to D0 state is a
				 * 2 step process. Dongle would want to know that D0 Inform
				 * would be sent as a MB interrupt to bring it out of D3 Ack
				 * state to D0 state. So we have to send both this message.
				 */

				/* Clear wait_for_d3_ack to send D0_INFORM or host_ready */
				bus->wait_for_d3_ack = 0;

				DHD_BUS_LOCK(bus->bus_lock, flags_bus);
				bus->bus_low_power_state = DHD_BUS_NO_LOW_POWER_STATE;
				/* Enable back the intmask which was cleared in DPC
				 * after getting D3_ACK.
				 */
				bus->resume_intr_enable_count++;

				/* For Linux, Macos etc (otherthan NDIS) enable back the dongle
				 * interrupts using intmask and host interrupts
				 * which were disabled in the dhdpcie_bus_isr()->
				 * dhd_bus_handle_d3_ack().
				 */
				/* Enable back interrupt using Intmask!! */
				dhdpcie_bus_intr_enable(bus);
				/* Enable back interrupt from Host side!! */
				dhdpcie_enable_irq(bus);

				DHD_BUS_UNLOCK(bus->bus_lock, flags_bus);

				if (bus->use_d0_inform) {
					DHD_OS_WAKE_LOCK_WAIVE(bus->dhd);
					dhdpcie_send_mb_data(bus,
						(H2D_HOST_D0_INFORM_IN_USE | H2D_HOST_D0_INFORM));
					DHD_OS_WAKE_LOCK_RESTORE(bus->dhd);
				}
				/* ring doorbell 1 (hostready) */
				dhd_bus_hostready(bus);

				DHD_GENERAL_LOCK(bus->dhd, flags);
				bus->dhd->busstate = DHD_BUS_DATA;
				/* resume all interface network queue. */
				dhd_bus_start_queue(bus);
				DHD_GENERAL_UNLOCK(bus->dhd, flags);
				rc = BCME_ERROR;
			} else {
				/* Actual Suspend after no wakelock */
#endif /* OEM_ANDROID */
				/* At this time bus->bus_low_power_state will be
				 * made to DHD_BUS_D3_ACK_RECIEVED after recieving D3_ACK
				 * in dhd_bus_handle_d3_ack()
				 */
				if (bus->use_d0_inform &&
					(bus->api.fw_rev < PCIE_SHARED_VERSION_6)) {
					DHD_OS_WAKE_LOCK_WAIVE(bus->dhd);
					dhdpcie_send_mb_data(bus, (H2D_HOST_D0_INFORM_IN_USE));
					DHD_OS_WAKE_LOCK_RESTORE(bus->dhd);
				}

#if defined(BCMPCIE_OOB_HOST_WAKE)
				if (bus->dhd->dhd_induce_error == DHD_INDUCE_DROP_OOB_IRQ) {
					DHD_ERROR(("%s: Inducing DROP OOB IRQ\n", __FUNCTION__));
				} else {
					dhdpcie_oob_intr_set(bus, TRUE);
				}
#endif /* BCMPCIE_OOB_HOST_WAKE */

				DHD_GENERAL_LOCK(bus->dhd, flags);
				/* The Host cannot process interrupts now so disable the same.
				 * No need to disable the dongle INTR using intmask, as we are
				 * already calling disabling INTRs from DPC context after
				 * getting D3_ACK in dhd_bus_handle_d3_ack.
				 * Code may not look symmetric between Suspend and
				 * Resume paths but this is done to close down the timing window
				 * between DPC and suspend context and bus->bus_low_power_state
				 * will be set to DHD_BUS_D3_ACK_RECIEVED in DPC.
				 */
				bus->dhd->d3ackcnt_timeout = 0;
				bus->dhd->busstate = DHD_BUS_SUSPEND;
				DHD_GENERAL_UNLOCK(bus->dhd, flags);
				dhdpcie_dump_resource(bus);
				/* Handle Host Suspend */
				rc = dhdpcie_pci_suspend_resume(bus, state);
				if (!rc) {
					bus->last_suspend_end_time = OSL_LOCALTIME_NS();
				}
#ifdef OEM_ANDROID
			}
#endif /* OEM_ANDROID */
		} else if (timeleft == 0) { /* D3 ACK Timeout */
#ifdef DHD_FW_COREDUMP
			uint32 cur_memdump_mode = bus->dhd->memdump_enabled;
#endif /* DHD_FW_COREDUMP */

			/* check if the D3 ACK timeout due to scheduling issue */
			bus->dhd->is_sched_error = !dhd_query_bus_erros(bus->dhd) &&
				bus->isr_entry_time > bus->last_d3_inform_time &&
				dhd_bus_query_dpc_sched_errors(bus->dhd);
			bus->dhd->d3ack_timeout_occured = TRUE;
			/* If the D3 Ack has timeout */
			bus->dhd->d3ackcnt_timeout++;
			DHD_ERROR(("%s: resumed on timeout for D3 ACK%s d3_inform_cnt %d\n",
				__FUNCTION__, bus->dhd->is_sched_error ?
				" due to scheduling problem" : "", bus->dhd->d3ackcnt_timeout));
#if defined(DHD_KERNEL_SCHED_DEBUG) && defined(DHD_FW_COREDUMP)
			if (bus->dhd->is_sched_error && cur_memdump_mode == DUMP_MEMFILE_BUGON) {
				/* change g_assert_type to trigger Kernel panic */
				g_assert_type = 2;
				/* use ASSERT() to trigger panic */
				ASSERT(0);
			}
#endif /* DHD_KERNEL_SCHED_DEBUG && DHD_FW_COREDUMP */
			DHD_BUS_LOCK(bus->bus_lock, flags_bus);
			bus->bus_low_power_state = DHD_BUS_NO_LOW_POWER_STATE;
			DHD_BUS_UNLOCK(bus->bus_lock, flags_bus);
			DHD_GENERAL_LOCK(bus->dhd, flags);
			bus->dhd->busstate = DHD_BUS_DATA;
			/* resume all interface network queue. */
			dhd_bus_start_queue(bus);
			DHD_GENERAL_UNLOCK(bus->dhd, flags);
			if (!bus->dhd->dongle_trap_occured &&
				!bus->is_linkdown &&
				!bus->cto_triggered) {
				uint32 intstatus = 0;

				/* Check if PCIe bus status is valid */
				intstatus = si_corereg(bus->sih, bus->sih->buscoreidx,
					bus->pcie_mailbox_int, 0, 0);
				if (intstatus == (uint32)-1) {
					/* Invalidate PCIe bus status */
					bus->is_linkdown = 1;
				}

				dhd_bus_dump_console_buffer(bus);
				dhd_prot_debug_info_print(bus->dhd);
#ifdef DHD_FW_COREDUMP
				if (cur_memdump_mode) {
					/* write core dump to file */
					bus->dhd->memdump_type = DUMP_TYPE_D3_ACK_TIMEOUT;
					dhdpcie_mem_dump(bus);
				}
#endif /* DHD_FW_COREDUMP */

#ifdef OEM_ANDROID
				DHD_ERROR(("%s: Event HANG send up due to D3_ACK timeout\n",
					__FUNCTION__));
#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
				bus->no_cfg_restore = 1;
#endif /* CONFIG_ARCH_MSM */
#endif /* SUPPORT_LINKDOWN_RECOVERY */
				dhd_os_check_hang(bus->dhd, 0, -ETIMEDOUT);
#endif /* OEM_ANDROID */
			}
#if defined(DHD_ERPOM)
			dhd_schedule_reset(bus->dhd);
#endif // endif
			rc = -ETIMEDOUT;
		}
	} else {
		/* Resume */
		DHD_ERROR(("%s: Entering resume state\n", __FUNCTION__));
		bus->last_resume_start_time = OSL_LOCALTIME_NS();

		/**
		 * PCIE2_BAR0_CORE2_WIN gets reset after D3 cold.
		 * si_backplane_access(function to read/write backplane)
		 * updates the window(PCIE2_BAR0_CORE2_WIN) only if
		 * window being accessed is different form the window
		 * being pointed by second_bar0win.
		 * Since PCIE2_BAR0_CORE2_WIN is already reset because of D3 cold,
		 * invalidating second_bar0win after resume updates
		 * PCIE2_BAR0_CORE2_WIN with right window.
		 */
		si_invalidate_second_bar0win(bus->sih);
#if defined(OEM_ANDROID)
#if defined(BCMPCIE_OOB_HOST_WAKE)
		DHD_OS_OOB_IRQ_WAKE_UNLOCK(bus->dhd);
#endif /* BCMPCIE_OOB_HOST_WAKE */
#endif /* linux && OEM_ANDROID */
		rc = dhdpcie_pci_suspend_resume(bus, state);
		dhdpcie_dump_resource(bus);

		DHD_BUS_LOCK(bus->bus_lock, flags_bus);
		/* set bus_low_power_state to DHD_BUS_NO_LOW_POWER_STATE */
		bus->bus_low_power_state = DHD_BUS_NO_LOW_POWER_STATE;
		DHD_BUS_UNLOCK(bus->bus_lock, flags_bus);

		if (!rc && bus->dhd->busstate == DHD_BUS_SUSPEND) {
			if (bus->use_d0_inform) {
				DHD_OS_WAKE_LOCK_WAIVE(bus->dhd);
				dhdpcie_send_mb_data(bus, (H2D_HOST_D0_INFORM));
				DHD_OS_WAKE_LOCK_RESTORE(bus->dhd);
			}
			/* ring doorbell 1 (hostready) */
			dhd_bus_hostready(bus);
		}
		DHD_GENERAL_LOCK(bus->dhd, flags);
		bus->dhd->busstate = DHD_BUS_DATA;
#ifdef DHD_PCIE_RUNTIMEPM
		if (DHD_BUS_BUSY_CHECK_RPM_SUSPEND_DONE(bus->dhd)) {
			bus->bus_wake = 1;
			OSL_SMP_WMB();
			wake_up_interruptible(&bus->rpm_queue);
		}
#endif /* DHD_PCIE_RUNTIMEPM */
		/* resume all interface network queue. */
		dhd_bus_start_queue(bus);

		/* TODO: for NDIS also we need to use enable_irq in future */
		bus->resume_intr_enable_count++;

		/* For Linux, Macos etc (otherthan NDIS) enable back the dongle interrupts
		 * using intmask and host interrupts
		 * which were disabled in the dhdpcie_bus_isr()->dhd_bus_handle_d3_ack().
		 */
		dhdpcie_bus_intr_enable(bus); /* Enable back interrupt using Intmask!! */
		dhdpcie_enable_irq(bus); /* Enable back interrupt from Host side!! */

		DHD_GENERAL_UNLOCK(bus->dhd, flags);

		if (bus->dhd->dhd_watchdog_ms_backup) {
			DHD_ERROR(("%s: Enabling wdtick after resume\n",
				__FUNCTION__));
			dhd_os_wd_timer(bus->dhd, bus->dhd->dhd_watchdog_ms_backup);
		}

		bus->last_resume_end_time = OSL_LOCALTIME_NS();
		/* Update TCM rd index for EDL ring */
		DHD_EDL_RING_TCM_RD_UPDATE(bus->dhd);
	}
	return rc;
}

uint32
dhdpcie_force_alp(struct dhd_bus *bus, bool enable)
{
	ASSERT(bus && bus->sih);
	if (enable) {
	si_corereg(bus->sih, bus->sih->buscoreidx,
		OFFSETOF(sbpcieregs_t, u.pcie2.clk_ctl_st), CCS_FORCEALP, CCS_FORCEALP);
	} else {
		si_corereg(bus->sih, bus->sih->buscoreidx,
			OFFSETOF(sbpcieregs_t, u.pcie2.clk_ctl_st), CCS_FORCEALP, 0);
	}
	return 0;
}

/* set pcie l1 entry time: dhd pciereg 0x1004[22:16] */
uint32
dhdpcie_set_l1_entry_time(struct dhd_bus *bus, int l1_entry_time)
{
	uint reg_val;

	ASSERT(bus && bus->sih);

	si_corereg(bus->sih, bus->sih->buscoreidx, OFFSETOF(sbpcieregs_t, configaddr), ~0,
		0x1004);
	reg_val = si_corereg(bus->sih, bus->sih->buscoreidx,
		OFFSETOF(sbpcieregs_t, configdata), 0, 0);
	reg_val = (reg_val & ~(0x7f << 16)) | ((l1_entry_time & 0x7f) << 16);
	si_corereg(bus->sih, bus->sih->buscoreidx, OFFSETOF(sbpcieregs_t, configdata), ~0,
		reg_val);

	return 0;
}

static uint32
dhd_apply_d11_war_length(struct  dhd_bus *bus, uint32 len, uint32 d11_lpbk)
{
	uint16 chipid = si_chipid(bus->sih);
	if ((chipid == BCM4375_CHIP_ID ||
		chipid == BCM4362_CHIP_ID ||
		chipid == BCM43751_CHIP_ID ||
		chipid == BCM4377_CHIP_ID) &&
		(d11_lpbk != M2M_DMA_LPBK && d11_lpbk != M2M_NON_DMA_LPBK)) {
			len += 8;
	}
	DHD_ERROR(("%s: len %d\n", __FUNCTION__, len));
	return len;
}

/** Transfers bytes from host to dongle and to host again using DMA */
static int
dhdpcie_bus_dmaxfer_req(struct  dhd_bus *bus,
		uint32 len, uint32 srcdelay, uint32 destdelay,
		uint32 d11_lpbk, uint32 core_num, uint32 wait)
{
	int ret = 0;

	if (bus->dhd == NULL) {
		DHD_ERROR(("bus not inited\n"));
		return BCME_ERROR;
	}
	if (bus->dhd->prot == NULL) {
		DHD_ERROR(("prot is not inited\n"));
		return BCME_ERROR;
	}
	if (bus->dhd->busstate != DHD_BUS_DATA) {
		DHD_ERROR(("not in a readystate to LPBK  is not inited\n"));
		return BCME_ERROR;
	}

	if (len < 5 || len > 4194296) {
		DHD_ERROR(("len is too small or too large\n"));
		return BCME_ERROR;
	}

	len = dhd_apply_d11_war_length(bus, len, d11_lpbk);

	bus->dmaxfer_complete = FALSE;
	ret = dhdmsgbuf_dmaxfer_req(bus->dhd, len, srcdelay, destdelay,
		d11_lpbk, core_num);
	if (ret != BCME_OK || !wait) {
		DHD_INFO(("%s: dmaxfer req returns status %u; wait = %u\n", __FUNCTION__,
				ret, wait));
	} else {
		ret = dhd_os_dmaxfer_wait(bus->dhd, &bus->dmaxfer_complete);
		if (ret < 0)
			ret = BCME_NOTREADY;
	}

	return ret;

}

bool
dhd_bus_is_multibp_capable(struct dhd_bus *bus)
{
	return MULTIBP_CAP(bus->sih);
}

#define PCIE_REV_FOR_4378A0	66	/* dhd_bus_perform_flr_with_quiesce() causes problems */
#define PCIE_REV_FOR_4378B0	68

static int
dhdpcie_bus_download_state(dhd_bus_t *bus, bool enter)
{
	int bcmerror = 0;
	volatile uint32 *cr4_regs;
	bool do_flr;
	hs_addrs_t bl_hs_addrs = {NULL, NULL};

	if (bus->sih->chip == CYW55560_CHIP_ID) {
		/* Host bootloader handshake TCM/REGS addresses init */
		bcmerror = dhdpcie_dongle_host_get_handshake_address(bus->sih, bus->osh,
			&bl_hs_addrs);
		if (bcmerror) {
			DHD_ERROR(("%s: REGS/TCM addresses not initialized\n", __FUNCTION__));
			goto fail;
		}
	}

	if (!bus->sih) {
		DHD_ERROR(("%s: NULL sih!!\n", __FUNCTION__));
		return BCME_ERROR;
	}

	do_flr = ((bus->sih->buscorerev != PCIE_REV_FOR_4378A0) &&
			(bus->sih->buscorerev != PCIE_REV_FOR_4378B0));

	if (MULTIBP_ENAB(bus->sih) && !do_flr) {
		dhd_bus_pcie_pwr_req(bus);
	}

	/* To enter download state, disable ARM and reset SOCRAM.
	 * To exit download state, simply reset ARM (default is RAM boot).
	 */
	if (enter) {
#ifndef BCMQT	/* for performance reasons, skip the FLR for QT */
#endif /* !BCMQT */

		/* Make sure BAR1 maps to backplane address 0 */
		dhdpcie_setbar1win(bus, 0x00000000);
		bus->alp_only = TRUE;

		/* some chips (e.g. 43602) have two ARM cores, the CR4 is receives the firmware. */
		cr4_regs = si_setcore(bus->sih, ARMCR4_CORE_ID, 0);

		if (cr4_regs == NULL && !(si_setcore(bus->sih, ARM7S_CORE_ID, 0)) &&
		    !(si_setcore(bus->sih, ARMCM3_CORE_ID, 0)) &&
		    !(si_setcore(bus->sih, ARMCA7_CORE_ID, 0))) {
			DHD_ERROR(("%s: Failed to find ARM core!\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
			goto fail;
		}

		if (si_setcore(bus->sih, ARMCA7_CORE_ID, 0)) {
			/* Halt ARM & remove reset */
			si_core_reset(bus->sih, SICF_CPUHALT, SICF_CPUHALT);
			if (!(si_setcore(bus->sih, SYSMEM_CORE_ID, 0))) {
				DHD_ERROR(("%s: Failed to find SYSMEM core!\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}
			si_core_reset(bus->sih, 0, 0);
			/* reset last 4 bytes of RAM address. to be used for shared area */
			dhdpcie_init_shared_addr(bus);
		} else if (cr4_regs == NULL) { /* no CR4 present on chip */
			si_core_disable(bus->sih, 0);

			if (!(si_setcore(bus->sih, SOCRAM_CORE_ID, 0))) {
				DHD_ERROR(("%s: Failed to find SOCRAM core!\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}

			si_core_reset(bus->sih, 0, 0);

			/* Clear the top bit of memory */
			if (bus->ramsize) {
				uint32 zeros = 0;
				if (dhdpcie_bus_membytes(bus, TRUE, bus->ramsize - 4,
				                     (uint8*)&zeros, 4) < 0) {
					bcmerror = BCME_ERROR;
					goto fail;
				}
			}
		} else {
			/* For CR4,
			 * Halt ARM
			 * Remove ARM reset
			 * Read RAM base address [0x18_0000]
			 * [next] Download firmware
			 * [done at else] Populate the reset vector
			 * [done at else] Remove ARM halt
			*/

			if (bus->sih->chip == CYW55560_CHIP_ID) {

				/* Skip ARM halt and reset in case of 55560 */

				/* Bootloader host pre handshake function */
				if ((bcmerror = dhdpcie_dongle_host_pre_handshake(bus->sih,
					bus->osh, &bl_hs_addrs))) {
					DHD_ERROR(("%s: error %d dongle host pre handshake\n",
						__FUNCTION__, bcmerror));
					goto fail;
				}
				DHD_ERROR(("%s: dongle host pre handshake successful, dl FW\n",
					__FUNCTION__));

				/* Read PCIE shared structure here */
				/* This is necessary for console buffer initialization */
				if ((bcmerror = dhdpcie_readshared_console(bus)) < 0) {
					DHD_ERROR(("%s: Shared region not initialized\n",
						__FUNCTION__));
				}

				/* Console buffer read - First pass */
				if ((bcmerror = dhdpcie_bus_readconsole(bus)) < 0) {
					DHD_ERROR(("%s: First pass console buffer read failed\n",
						__FUNCTION__));
				}
			} else {
				/* Halt ARM & remove reset */
				si_core_reset(bus->sih, SICF_CPUHALT, SICF_CPUHALT);
				if (BCM43602_CHIP(bus->sih->chip)) {
					W_REG(bus->pcie_mb_intr_osh, cr4_regs + ARMCR4REG_BANKIDX,
						5);
					W_REG(bus->pcie_mb_intr_osh, cr4_regs + ARMCR4REG_BANKPDA,
						0);
					W_REG(bus->pcie_mb_intr_osh, cr4_regs + ARMCR4REG_BANKIDX,
						7);
					W_REG(bus->pcie_mb_intr_osh, cr4_regs + ARMCR4REG_BANKPDA,
						0);
				}
				/* reset last 4 bytes of RAM address. to be used for shared area */
				dhdpcie_init_shared_addr(bus);
			}
		}
	} else {
		if (si_setcore(bus->sih, ARMCA7_CORE_ID, 0)) {
			/* write vars */
			if ((bcmerror = dhdpcie_bus_write_vars(bus))) {
				DHD_ERROR(("%s: could not write vars to RAM\n", __FUNCTION__));
				goto fail;
			}
			/* write random numbers to sysmem for the purpose of
			 * randomizing heap address space.
			 */
			if ((bcmerror = dhdpcie_wrt_rnd(bus)) != BCME_OK) {
				DHD_ERROR(("%s: Failed to get random seed to write to TCM !\n",
					__FUNCTION__));
				goto fail;
			}
			/* switch back to arm core again */
			if (!(si_setcore(bus->sih, ARMCA7_CORE_ID, 0))) {
				DHD_ERROR(("%s: Failed to find ARM CA7 core!\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}
			/* write address 0 with reset instruction */
			bcmerror = dhdpcie_bus_membytes(bus, TRUE, 0,
				(uint8 *)&bus->resetinstr, sizeof(bus->resetinstr));
			/* now remove reset and halt and continue to run CA7 */
		} else if (!si_setcore(bus->sih, ARMCR4_CORE_ID, 0)) {
			if (!(si_setcore(bus->sih, SOCRAM_CORE_ID, 0))) {
				DHD_ERROR(("%s: Failed to find SOCRAM core!\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}

			if (!si_iscoreup(bus->sih)) {
				DHD_ERROR(("%s: SOCRAM core is down after reset?\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}

			/* Enable remap before ARM reset but after vars.
			 * No backplane access in remap mode
			 */
			if (!si_setcore(bus->sih, PCMCIA_CORE_ID, 0) &&
			    !si_setcore(bus->sih, SDIOD_CORE_ID, 0)) {
				DHD_ERROR(("%s: Can't change back to SDIO core?\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}

			if (!(si_setcore(bus->sih, ARM7S_CORE_ID, 0)) &&
			    !(si_setcore(bus->sih, ARMCM3_CORE_ID, 0))) {
				DHD_ERROR(("%s: Failed to find ARM core!\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}
		} else {
			if (BCM43602_CHIP(bus->sih->chip)) {
				/* Firmware crashes on SOCSRAM access when core is in reset */
				if (!(si_setcore(bus->sih, SOCRAM_CORE_ID, 0))) {
					DHD_ERROR(("%s: Failed to find SOCRAM core!\n",
						__FUNCTION__));
					bcmerror = BCME_ERROR;
					goto fail;
				}
				si_core_reset(bus->sih, 0, 0);
				si_setcore(bus->sih, ARMCR4_CORE_ID, 0);
			}

			if (bus->sih->chip == CYW55560_CHIP_ID) {
				/* Console buffer read - Second pass */
				if ((bcmerror = dhdpcie_bus_readconsole(bus)) < 0) {
					DHD_ERROR(("%s: Second pass console buffer read failed\n",
						__FUNCTION__));
				}

				/* FW and NVRAM download done notification to bootloader */
				if ((bcmerror = dhdpcie_dongle_host_post_handshake(bus->sih,
					bus->osh, &bl_hs_addrs))) {
					DHD_ERROR(("%s: error %d dongle host post handshake\n",
						__FUNCTION__, bcmerror));
					goto fail;
				}
				DHD_ERROR(("%s: FW download successful\n", __FUNCTION__));

				/*
				 * Check signature validation function
				 * D2H_VALDN_DONE bit will be set in the following cases:
				 * 1. Open mode: when a signature is not sent
				 * 2. Secure mode: when a valid signature is sent
				 * Write vars and nvram download only if the D2H_VALDN_DONE
				 * bit has been set
				 */

				if ((bcmerror = dhdpcie_dongle_host_chk_validation(bus->sih,
					bus->osh, &bl_hs_addrs))) {
					DHD_ERROR(("%s: error %d dongle host validation\n",
						__FUNCTION__, bcmerror));
					goto fail;
				}
			}

			/* write vars */
			if ((bcmerror = dhdpcie_bus_write_vars(bus))) {
				DHD_ERROR(("%s: could not write vars to RAM\n", __FUNCTION__));
				goto fail;
			}

			/* write a random number to TCM for the purpose of
			 * randomizing heap address space.
			 */
			if ((bcmerror = dhdpcie_wrt_rnd(bus)) != BCME_OK) {
				DHD_ERROR(("%s: Failed to get random seed to write to TCM !\n",
					__FUNCTION__));
				goto fail;
			}

			/* switch back to arm core again */
			if (!(si_setcore(bus->sih, ARMCR4_CORE_ID, 0))) {
				DHD_ERROR(("%s: Failed to find ARM CR4 core!\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}

			/* write address 0 with reset instruction */
			if (bus->sih->chip != CYW55560_CHIP_ID) {
				bcmerror = dhdpcie_bus_membytes(bus, TRUE, 0,
					(uint8 *)&bus->resetinstr, sizeof(bus->resetinstr));

				if (bcmerror == BCME_OK) {
					uint32 tmp;

					bcmerror = dhdpcie_bus_membytes(bus, FALSE, 0,
						(uint8 *)&tmp, sizeof(tmp));

					if (bcmerror == BCME_OK && tmp != bus->resetinstr) {
						DHD_ERROR(("%s: Failed to write 0x%08x to addr 0\n",
							__FUNCTION__, bus->resetinstr));
						DHD_ERROR(("%s: contents of addr 0 is 0x%08x\n",
							__FUNCTION__, tmp));
						bcmerror = BCME_ERROR;
						goto fail;
					}
				}
			}

			/* now remove reset and halt and continue to run CR4 */
		}

		if (bus->sih->chip == CYW55560_CHIP_ID) {
			/* Console buffer read - Final pass */
			if ((bcmerror = dhdpcie_bus_readconsole(bus)) < 0) {
				DHD_ERROR(("%s: Final pass console buffer read failed\n",
					__FUNCTION__));
			}

			/* Set write_vars done bit to let BL jump to mainline FW */
			if ((bcmerror = dhdpcie_dongle_host_post_varswrite(bus, &bl_hs_addrs))) {
					DHD_ERROR(("%s: error %d dongle_host_post_varswrite\n",
					__FUNCTION__, bcmerror));
				goto fail;
			}
			DHD_ERROR(("%s VARS done bit set, BL can jump to mainline FW\n",
				__FUNCTION__));
		} else {
			si_core_reset(bus->sih, 0, 0);
		}
		/* Allow HT Clock now that the ARM is running. */
		bus->alp_only = FALSE;

		bus->dhd->busstate = DHD_BUS_LOAD;
	}

fail:

	if (bcmerror) {
		if (bus->sih->chip == CYW55560_CHIP_ID) {
			/* Read the shared structure to determine console address */
			if (dhdpcie_readshared_console(bus) < 0) {
				DHD_ERROR(("%s: Shared region not initialized\n",
					__FUNCTION__));
			} else {
				/* Console buffer read */
				if (dhdpcie_bus_readconsole(bus) < 0) {
					DHD_ERROR(("%s: Failure case console buffer read failed\n",
						__FUNCTION__));
				}
			}
		}
	}

	/* Always return to PCIE core */
	si_setcore(bus->sih, PCIE2_CORE_ID, 0);

	if (MULTIBP_ENAB(bus->sih) && !do_flr) {
		dhd_bus_pcie_pwr_req_clear(bus);
	}

	return bcmerror;
} /* dhdpcie_bus_download_state */

static int
dhdpcie_dongle_host_get_handshake_address(si_t *sih, osl_t *osh, hs_addrs_t *addr)
{
	int bcmerror = BCME_OK;

#ifndef HS_IN_TCM
	sbpcieregs_t *pcieregs;

	pcieregs = si_setcore(sih, PCIE2_CORE_ID, 0);
	if (!pcieregs) {
		return BCME_ERROR;
	}
	addr->d2h = &pcieregs->u1.dar_64.d2h_msg_reg0;
	addr->h2d = &pcieregs->u1.dar_64.h2d_msg_reg0;
#else /* HS_IN_TCM */
	addr->d2h = (void *)HS_IN_TCM;
	addr->h2d = (volatile uint32 *)addr->d2h + 1;
#endif /* HS_IN_TCM */

	return bcmerror;
} /* dhdpcie_dongle_host_get_handshake_address */

static int
dhdpcie_handshake_msg_reg_write(si_t *sih, osl_t *osh, volatile void *addr, uint *buffer)
{
	int bcmerror = BCME_OK;

#ifndef HS_IN_TCM
	si_setcore(sih, PCIE2_CORE_ID, 0);
	W_REG(osh, (volatile uint32 *)addr, *buffer);
#else
	bcmerror = si_backplane_access(sih, addr, 4, buffer, FALSE);
#endif // endif
	return bcmerror;
} /* dhdpcie_handshake_msg_reg_write */

static int
dhdpcie_handshake_msg_reg_read(si_t *sih, osl_t *osh, volatile void *addr, uint *buffer)
{
	int bcmerror = BCME_OK;

#ifndef HS_IN_TCM
	si_setcore(sih, PCIE2_CORE_ID, 0);
	*buffer = R_REG(osh, (volatile uint32 *)addr);
#else
	bcmerror = si_backplane_access(sih, addr, 4, buffer, TRUE);
#endif // endif
	return bcmerror;
} /* dhdpcie_handshake_msg_reg_read */

static int
dhdpcie_dongle_host_handshake_spinwait(si_t *sih, osl_t *osh, volatile void *addr, uint32 bitshift,
	uint32 us)
{
	uint32 countdown_;
	uint32 read_addr = 0;
	int bcmerror = BCME_OK;

	for (countdown_ = (us) + (HS_POLL_PERIOD_US - 1U); countdown_ >= HS_POLL_PERIOD_US;
		countdown_ -= HS_POLL_PERIOD_US) {

		bcmerror = dhdpcie_handshake_msg_reg_read(sih, osh, addr, &read_addr);
		if (bcmerror) {
			bcmerror = BCME_ERROR;
			break;
		}

		if (isset(&read_addr, bitshift)) {
			bcmerror = BCME_OK;
			break;
		}

		OSL_DELAY(HS_POLL_PERIOD_US);
	}

	if (countdown_ <= HS_POLL_PERIOD_US) {
		bcmerror = BCME_NOTREADY;
	}

	return bcmerror;
} /* dhdpcie_dongle_host_handshake_spinwait */

static int
dhdpcie_dongle_host_pre_handshake(si_t *sih, osl_t *osh, hs_addrs_t *addr)
{
	int bcmerror = BCME_OK;
	int h2d_reg = 0x00000000;

	/* Host initialization for dongle to host handshake */
	bcmerror = dhdpcie_handshake_msg_reg_write(sih, osh, addr->h2d, &h2d_reg);
	if (bcmerror) {
		goto err;
	}

	bcmerror = dhdpcie_dongle_host_handshake_spinwait(sih, osh, addr->d2h, D2H_READY_SHIFT,
		D2H_READY_TIMEOUT_US);
	if (!bcmerror) {

		/* Set H2D_DL_START indication to dongle that Host shall start FW download */
		h2d_reg = 0;
		setbit(&h2d_reg, H2D_DL_START_SHIFT);
		bcmerror = dhdpcie_handshake_msg_reg_write(sih, osh, addr->h2d, &h2d_reg);
		if (bcmerror) {
			goto err;
		}
	}

err:
	return bcmerror;
} /* dhdpcie_dongle_host_pre_handshake */

static int
dhdpcie_dongle_host_post_handshake(si_t *sih, osl_t *osh, hs_addrs_t *addr)
{
	int bcmerror = BCME_OK;
	int h2d_reg = 0x00000000;

	/* Reset download start */
	clrbit(&h2d_reg, H2D_DL_START_SHIFT);

	/* download done */
	setbit(&h2d_reg, H2D_DL_DONE_SHIFT);
	bcmerror = dhdpcie_handshake_msg_reg_write(sih, osh, addr->h2d, &h2d_reg);
	if (bcmerror) {
		goto err;
	}

	bcmerror = dhdpcie_dongle_host_handshake_spinwait(sih, osh, addr->d2h,
		D2H_TRX_HDR_PARSE_DONE_SHIFT, D2H_TRX_HDR_PARSE_DONE_TIMEOUT_US);

	if (bcmerror) {
		/* Host notification to bootloader to get reset on error */
		dhdpcie_handshake_msg_reg_read(sih, osh, addr->h2d, &h2d_reg);
		setbit(&h2d_reg, H2D_BL_RESET_ON_ERROR_SHIFT);
		dhdpcie_handshake_msg_reg_write(sih, osh, addr->h2d, &h2d_reg);
	}

err:
	return bcmerror;
} /* dhdpcie_dongle_host_post_handshake */

static int
dhdpcie_dongle_host_chk_validation(si_t *sih, osl_t *osh, hs_addrs_t *addr)
{
	int bcmerror = BCME_OK;
	uint d2h_reg = 0x00000000;
	uint h2d_reg = 0x00000000;

	bcmerror = dhdpcie_dongle_host_handshake_spinwait(sih, osh, addr->d2h, D2H_VALDN_DONE_SHIFT,
		D2H_VALDN_DONE_TIMEOUT_US);
	if (!bcmerror) {

		bcmerror = dhdpcie_handshake_msg_reg_read(sih, osh, addr->d2h, &d2h_reg);
		if (!bcmerror) {

			if (isset(&d2h_reg, D2H_VALDN_RESULT_SHIFT)) {
				DHD_ERROR(("%s: TRX img validation check successful\n",
				__FUNCTION__));
			} else {
				DHD_ERROR(("%s: TRX img validation check failed\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
			}
		}
	}

	if (bcmerror) {
		/* Host notification to bootloader to get reset on error
		 * To avoid the race condition betweeen host and dongle
		 */
		dhdpcie_handshake_msg_reg_read(sih, osh, addr->h2d, &h2d_reg);
		setbit(&h2d_reg, H2D_BL_RESET_ON_ERROR_SHIFT);
		dhdpcie_handshake_msg_reg_write(sih, osh, addr->h2d, &h2d_reg);
	}

	return bcmerror;
} /* dhdpcie_dongle_host_chk_validation */

int
dhdpcie_dongle_host_pre_wd_reset_sequence(si_t *sih, osl_t *osh)
{
	int32 bcmerror = BCME_ERROR;
	sbpcieregs_t *pcieregs = NULL;
	uint32 reg_val = 0;

	if (sih && osh) {

		pcieregs = (sbpcieregs_t *)si_setcore(sih, PCIE2_CORE_ID, 0);

		/* Host initialization for dongle to host handshake */
		bcmerror = dhdpcie_handshake_msg_reg_write(sih, osh,
			&pcieregs->u1.dar_64.h2d_msg_reg0, &reg_val);
	}

	return bcmerror;
} /* dhdpcie_dongle_host_pre_wd_reset_sequence */

int
dhdpcie_dongle_host_post_wd_reset_sequence(si_t *sih, osl_t *osh)
{
	int32 bcmerror = BCME_ERROR;
	sbpcieregs_t *pcieregs = NULL;
	uint32 reg_val = 0;
	int32 idx = 0;
	int print_interval = D2H_READY_WD_RESET_COUNT / 10;

	if (sih && osh) {
		pcieregs = (sbpcieregs_t *)si_setcore(sih, PCIE2_CORE_ID, 0);

		/* Host initialization for dongle to host handshake */
		bcmerror = dhdpcie_handshake_msg_reg_write(sih, osh,
			&pcieregs->u1.dar_64.h2d_msg_reg0, &reg_val);

		for (idx = D2H_READY_WD_RESET_COUNT; idx > 0; idx--) {

#ifdef BCMQT
			OSL_SLEEP(D2H_READY_WD_RESET_US/1000);
#else
			OSL_DELAY(D2H_READY_WD_RESET_US);
#endif // endif
			if (!(idx % print_interval)) {
				DHD_ERROR(("Waiting %d us for D2H_READY\n",
					idx * D2H_READY_WD_RESET_US));
			}

			dhdpcie_handshake_msg_reg_read(sih, osh, &pcieregs->u1.dar_64.d2h_msg_reg0,
				&reg_val);
			if (isset(&reg_val, D2H_READY_SHIFT)) {
				break;
			}
		}

		if (!idx) {
			DHD_ERROR(("%s: error - Waiting for D2H_READY timeout %d\n",
				__FUNCTION__, idx));
		} else {
			bcmerror = BCME_OK;
		}
	}

	return bcmerror;
} /* dhdpcie_dongle_host_post_wd_reset_sequence */

/* Pre ChipId access sequence making sure that H2D HS reg is cleared and
 * host waited for bootloader to be ready before chipid access.
 */
int
dhdpcie_dongle_host_pre_chipid_access_sequence(osl_t *osh, volatile void *regva)
{
	int32 bcmerror = BCME_ERROR;
	sbpcieregs_t *pcieregs = NULL;
	uint32 reg_val = 0;
	int32 idx = 0;
	int print_interval = D2H_READY_WD_RESET_COUNT / 10;

	if (osh && regva) {

		pcieregs = (sbpcieregs_t*)(regva);

		/* Host init for D2H handshake */
		W_REG(osh, &pcieregs->u1.dar_64.h2d_msg_reg0, reg_val);

		/* Host waits for bootloader to be ready before ChipId access */
		for (idx = D2H_READY_WD_RESET_COUNT; idx > 0; idx--) {

#ifdef BCMQT
			OSL_SLEEP(D2H_READY_WD_RESET_US/1000);
#else
			OSL_DELAY(D2H_READY_WD_RESET_US);
#endif // endif
			if (!(idx % print_interval)) {
				DHD_ERROR(("Waiting %d us for D2H_READY\n",
					idx * D2H_READY_WD_RESET_US));
			}
			reg_val = R_REG(osh, &pcieregs->u1.dar_64.d2h_msg_reg0);
			if (isset(&reg_val, D2H_READY_SHIFT)) {
				break;
			}
		}

		if (!idx) {
			DHD_ERROR(("%s: error - Waiting for D2H_READY timeout %d\n",
				__FUNCTION__, idx));
		} else {
			bcmerror = BCME_OK;
		}
	}

	return bcmerror;
} /* dhdpcie_dongle_host_pre_chipid_access_sequence */

static int
dhdpcie_dongle_host_post_varswrite(dhd_bus_t *bus, hs_addrs_t *addr)
{
	int bcmerror = BCME_OK;
	uint h2d_reg = 0x00000000;

	/* Set NVRAM done bit (Download done is already set) */
	setbit(&h2d_reg, H2D_DL_DONE_SHIFT);
	setbit(&h2d_reg, H2D_DL_NVRAM_DONE_SHIFT);
	bcmerror = dhdpcie_handshake_msg_reg_write(bus->sih, bus->osh, addr->h2d, &h2d_reg);

	return bcmerror;
} /* dhdpcie_dongle_host_post_varswrite */

static int
dhdpcie_bus_write_vars(dhd_bus_t *bus)
{
	int bcmerror = 0;
	uint32 varsize, phys_size;
	uint32 varaddr;
	uint8 *vbuffer;
	uint32 varsizew;
#ifdef DHD_DEBUG
	uint8 *nvram_ularray;
#endif /* DHD_DEBUG */

	/* Even if there are no vars are to be written, we still need to set the ramsize. */
	varsize = bus->varsz ? ROUNDUP(bus->varsz, 4) : 0;
	varaddr = (bus->ramsize - 4) - varsize;

	varaddr += bus->dongle_ram_base;

	if (bus->vars) {

		vbuffer = (uint8 *)MALLOC(bus->dhd->osh, varsize);
		if (!vbuffer)
			return BCME_NOMEM;

		bzero(vbuffer, varsize);
		bcopy(bus->vars, vbuffer, bus->varsz);
		/* Write the vars list */
		bcmerror = dhdpcie_bus_membytes(bus, TRUE, varaddr, vbuffer, varsize);

		/* Implement read back and verify later */
#ifdef DHD_DEBUG
		/* Verify NVRAM bytes */
		DHD_INFO(("Compare NVRAM dl & ul; varsize=%d\n", varsize));
		nvram_ularray = (uint8*)MALLOC(bus->dhd->osh, varsize);
		if (!nvram_ularray) {
			MFREE(bus->dhd->osh, vbuffer, varsize);
			return BCME_NOMEM;
		}

		/* Upload image to verify downloaded contents. */
		memset(nvram_ularray, 0xaa, varsize);

		/* Read the vars list to temp buffer for comparison */
		bcmerror = dhdpcie_bus_membytes(bus, FALSE, varaddr, nvram_ularray, varsize);
		if (bcmerror) {
				DHD_ERROR(("%s: error %d on reading %d nvram bytes at 0x%08x\n",
					__FUNCTION__, bcmerror, varsize, varaddr));
		}

		/* Compare the org NVRAM with the one read from RAM */
		if (memcmp(vbuffer, nvram_ularray, varsize)) {
			DHD_ERROR(("%s: Downloaded NVRAM image is corrupted.\n", __FUNCTION__));
		} else
			DHD_ERROR(("%s: Download, Upload and compare of NVRAM succeeded.\n",
			__FUNCTION__));

		MFREE(bus->dhd->osh, nvram_ularray, varsize);
#endif /* DHD_DEBUG */

		MFREE(bus->dhd->osh, vbuffer, varsize);
	}

	phys_size = REMAP_ENAB(bus) ? bus->ramsize : bus->orig_ramsize;

	phys_size += bus->dongle_ram_base;

	/* adjust to the user specified RAM */
	DHD_INFO(("Physical memory size: %d, usable memory size: %d\n",
		phys_size, bus->ramsize));
	DHD_INFO(("Vars are at %d, orig varsize is %d\n",
		varaddr, varsize));
	varsize = ((phys_size - 4) - varaddr);

	/*
	 * Determine the length token:
	 * Varsize, converted to words, in lower 16-bits, checksum in upper 16-bits.
	 */
	if (bcmerror) {
		varsizew = 0;
		bus->nvram_csm = varsizew;
	} else {
		varsizew = varsize / 4;
		varsizew = (~varsizew << 16) | (varsizew & 0x0000FFFF);
		bus->nvram_csm = varsizew;
		varsizew = htol32(varsizew);
	}

	DHD_INFO(("New varsize is %d, length token=0x%08x\n", varsize, varsizew));

	/* Write the length token to the last word */
	bcmerror = dhdpcie_bus_membytes(bus, TRUE, (phys_size - 4),
		(uint8*)&varsizew, 4);

	return bcmerror;
} /* dhdpcie_bus_write_vars */

int
dhdpcie_downloadvars(dhd_bus_t *bus, void *arg, int len)
{
	int bcmerror = BCME_OK;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	/* Basic sanity checks */
	if (bus->dhd->up) {
		bcmerror = BCME_NOTDOWN;
		goto err;
	}
	if (!len) {
		bcmerror = BCME_BUFTOOSHORT;
		goto err;
	}

	/* Free the old ones and replace with passed variables */
	if (bus->vars)
		MFREE(bus->dhd->osh, bus->vars, bus->varsz);

	bus->vars = MALLOC(bus->dhd->osh, len);
	bus->varsz = bus->vars ? len : 0;
	if (bus->vars == NULL) {
		bcmerror = BCME_NOMEM;
		goto err;
	}

	/* Copy the passed variables, which should include the terminating double-null */
	bcopy(arg, bus->vars, bus->varsz);

#ifdef DHD_USE_SINGLE_NVRAM_FILE
	if (dhd_bus_get_fw_mode(bus->dhd) == DHD_FLAG_MFG_MODE) {
		char *sp = NULL;
		char *ep = NULL;
		int i;
		char tag[2][8] = {"ccode=", "regrev="};

		/* Find ccode and regrev info */
		for (i = 0; i < 2; i++) {
			sp = strnstr(bus->vars, tag[i], bus->varsz);
			if (!sp) {
				DHD_ERROR(("%s: Could not find ccode info from the nvram %s\n",
					__FUNCTION__, bus->nv_path));
				bcmerror = BCME_ERROR;
				goto err;
			}
			sp = strchr(sp, '=');
			ep = strchr(sp, '\0');
			/* We assumed that string length of both ccode and
			 * regrev values should not exceed WLC_CNTRY_BUF_SZ
			 */
			if (ep && ((ep - sp) <= WLC_CNTRY_BUF_SZ)) {
				sp++;
				while (*sp != '\0') {
					DHD_INFO(("%s: parse '%s', current sp = '%c'\n",
						__FUNCTION__, tag[i], *sp));
					*sp++ = '0';
				}
			} else {
				DHD_ERROR(("%s: Invalid parameter format when parsing for %s\n",
					__FUNCTION__, tag[i]));
				bcmerror = BCME_ERROR;
				goto err;
			}
		}
	}
#endif /* DHD_USE_SINGLE_NVRAM_FILE */

err:
	return bcmerror;
}

/* loop through the capability list and see if the pcie capabilty exists */
uint8
dhdpcie_find_pci_capability(osl_t *osh, uint8 req_cap_id)
{
	uint8 cap_id;
	uint8 cap_ptr = 0;
	uint8 byte_val;

	/* check for Header type 0 */
	byte_val = read_pci_cfg_byte(PCI_CFG_HDR);
	if ((byte_val & 0x7f) != PCI_HEADER_NORMAL) {
		DHD_ERROR(("%s : PCI config header not normal.\n", __FUNCTION__));
		goto end;
	}

	/* check if the capability pointer field exists */
	byte_val = read_pci_cfg_byte(PCI_CFG_STAT);
	if (!(byte_val & PCI_CAPPTR_PRESENT)) {
		DHD_ERROR(("%s : PCI CAP pointer not present.\n", __FUNCTION__));
		goto end;
	}

	cap_ptr = read_pci_cfg_byte(PCI_CFG_CAPPTR);
	/* check if the capability pointer is 0x00 */
	if (cap_ptr == 0x00) {
		DHD_ERROR(("%s : PCI CAP pointer is 0x00.\n", __FUNCTION__));
		goto end;
	}

	/* loop thr'u the capability list and see if the pcie capabilty exists */

	cap_id = read_pci_cfg_byte(cap_ptr);

	while (cap_id != req_cap_id) {
		cap_ptr = read_pci_cfg_byte((cap_ptr + 1));
		if (cap_ptr == 0x00) break;
		cap_id = read_pci_cfg_byte(cap_ptr);
	}

end:
	return cap_ptr;
}

void
dhdpcie_pme_active(osl_t *osh, bool enable)
{
	uint8 cap_ptr;
	uint32 pme_csr;

	cap_ptr = dhdpcie_find_pci_capability(osh, PCI_CAP_POWERMGMTCAP_ID);

	if (!cap_ptr) {
		DHD_ERROR(("%s : Power Management Capability not present\n", __FUNCTION__));
		return;
	}

	pme_csr = OSL_PCI_READ_CONFIG(osh, cap_ptr + PME_CSR_OFFSET, sizeof(uint32));
	DHD_ERROR(("%s : pme_sts_ctrl 0x%x\n", __FUNCTION__, pme_csr));

	pme_csr |= PME_CSR_PME_STAT;
	if (enable) {
		pme_csr |= PME_CSR_PME_EN;
	} else {
		pme_csr &= ~PME_CSR_PME_EN;
	}

	OSL_PCI_WRITE_CONFIG(osh, cap_ptr + PME_CSR_OFFSET, sizeof(uint32), pme_csr);
}

bool
dhdpcie_pme_cap(osl_t *osh)
{
	uint8 cap_ptr;
	uint32 pme_cap;

	cap_ptr = dhdpcie_find_pci_capability(osh, PCI_CAP_POWERMGMTCAP_ID);

	if (!cap_ptr) {
		DHD_ERROR(("%s : Power Management Capability not present\n", __FUNCTION__));
		return FALSE;
	}

	pme_cap = OSL_PCI_READ_CONFIG(osh, cap_ptr, sizeof(uint32));

	DHD_ERROR(("%s : pme_cap 0x%x\n", __FUNCTION__, pme_cap));

	return ((pme_cap & PME_CAP_PM_STATES) != 0);
}

uint32
dhdpcie_lcreg(osl_t *osh, uint32 mask, uint32 val)
{

	uint8	pcie_cap;
	uint8	lcreg_offset;	/* PCIE capability LCreg offset in the config space */
	uint32	reg_val;

	pcie_cap = dhdpcie_find_pci_capability(osh, PCI_CAP_PCIECAP_ID);

	if (!pcie_cap) {
		DHD_ERROR(("%s : PCIe Capability not present\n", __FUNCTION__));
		return 0;
	}

	lcreg_offset = pcie_cap + PCIE_CAP_LINKCTRL_OFFSET;

	/* set operation */
	if (mask) {
		/* read */
		reg_val = OSL_PCI_READ_CONFIG(osh, lcreg_offset, sizeof(uint32));

		/* modify */
		reg_val &= ~mask;
		reg_val |= (mask & val);

		/* write */
		OSL_PCI_WRITE_CONFIG(osh, lcreg_offset, sizeof(uint32), reg_val);
	}
	return OSL_PCI_READ_CONFIG(osh, lcreg_offset, sizeof(uint32));
}

uint8
dhdpcie_clkreq(osl_t *osh, uint32 mask, uint32 val)
{
	uint8	pcie_cap;
	uint32	reg_val;
	uint8	lcreg_offset;	/* PCIE capability LCreg offset in the config space */

	pcie_cap = dhdpcie_find_pci_capability(osh, PCI_CAP_PCIECAP_ID);

	if (!pcie_cap) {
		DHD_ERROR(("%s : PCIe Capability not present\n", __FUNCTION__));
		return 0;
	}

	lcreg_offset = pcie_cap + PCIE_CAP_LINKCTRL_OFFSET;

	reg_val = OSL_PCI_READ_CONFIG(osh, lcreg_offset, sizeof(uint32));
	/* set operation */
	if (mask) {
		if (val)
			reg_val |= PCIE_CLKREQ_ENAB;
		else
			reg_val &= ~PCIE_CLKREQ_ENAB;
		OSL_PCI_WRITE_CONFIG(osh, lcreg_offset, sizeof(uint32), reg_val);
		reg_val = OSL_PCI_READ_CONFIG(osh, lcreg_offset, sizeof(uint32));
	}
	if (reg_val & PCIE_CLKREQ_ENAB)
		return 1;
	else
		return 0;
}

void dhd_dump_intr_counters(dhd_pub_t *dhd, struct bcmstrbuf *strbuf)
{
	dhd_bus_t *bus;
	uint64 current_time = OSL_LOCALTIME_NS();

	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	bus = dhd->bus;
	if (!bus) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return;
	}

	bcm_bprintf(strbuf, "\n ------- DUMPING INTR enable/disable counters-------\n");
	bcm_bprintf(strbuf, "resume_intr_enable_count=%lu dpc_intr_enable_count=%lu\n"
		"isr_intr_disable_count=%lu suspend_intr_disable_count=%lu\n"
		"dpc_return_busdown_count=%lu non_ours_irq_count=%lu\n",
		bus->resume_intr_enable_count, bus->dpc_intr_enable_count,
		bus->isr_intr_disable_count, bus->suspend_intr_disable_count,
		bus->dpc_return_busdown_count, bus->non_ours_irq_count);
#ifdef BCMPCIE_OOB_HOST_WAKE
	bcm_bprintf(strbuf, "oob_intr_count=%lu oob_intr_enable_count=%lu"
		" oob_intr_disable_count=%lu\noob_irq_num=%d last_oob_irq_time="SEC_USEC_FMT
		" last_oob_irq_enable_time="SEC_USEC_FMT"\nlast_oob_irq_disable_time="SEC_USEC_FMT
		" oob_irq_enabled=%d oob_gpio_level=%d\n",
		bus->oob_intr_count, bus->oob_intr_enable_count,
		bus->oob_intr_disable_count, dhdpcie_get_oob_irq_num(bus),
		GET_SEC_USEC(bus->last_oob_irq_time), GET_SEC_USEC(bus->last_oob_irq_enable_time),
		GET_SEC_USEC(bus->last_oob_irq_disable_time), dhdpcie_get_oob_irq_status(bus),
		dhdpcie_get_oob_irq_level());
#endif /* BCMPCIE_OOB_HOST_WAKE */
	bcm_bprintf(strbuf, "\ncurrent_time="SEC_USEC_FMT" isr_entry_time="SEC_USEC_FMT
		" isr_exit_time="SEC_USEC_FMT"\ndpc_sched_time="SEC_USEC_FMT
		" last_non_ours_irq_time="SEC_USEC_FMT" dpc_entry_time="SEC_USEC_FMT"\n"
		"last_process_ctrlbuf_time="SEC_USEC_FMT " last_process_flowring_time="SEC_USEC_FMT
		" last_process_txcpl_time="SEC_USEC_FMT"\nlast_process_rxcpl_time="SEC_USEC_FMT
		" last_process_infocpl_time="SEC_USEC_FMT" last_process_edl_time="SEC_USEC_FMT
		"\ndpc_exit_time="SEC_USEC_FMT" resched_dpc_time="SEC_USEC_FMT"\n"
		"last_d3_inform_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(current_time), GET_SEC_USEC(bus->isr_entry_time),
		GET_SEC_USEC(bus->isr_exit_time), GET_SEC_USEC(bus->dpc_sched_time),
		GET_SEC_USEC(bus->last_non_ours_irq_time), GET_SEC_USEC(bus->dpc_entry_time),
		GET_SEC_USEC(bus->last_process_ctrlbuf_time),
		GET_SEC_USEC(bus->last_process_flowring_time),
		GET_SEC_USEC(bus->last_process_txcpl_time),
		GET_SEC_USEC(bus->last_process_rxcpl_time),
		GET_SEC_USEC(bus->last_process_infocpl_time),
		GET_SEC_USEC(bus->last_process_edl_time),
		GET_SEC_USEC(bus->dpc_exit_time), GET_SEC_USEC(bus->resched_dpc_time),
		GET_SEC_USEC(bus->last_d3_inform_time));

	bcm_bprintf(strbuf, "\nlast_suspend_start_time="SEC_USEC_FMT" last_suspend_end_time="
		SEC_USEC_FMT" last_resume_start_time="SEC_USEC_FMT" last_resume_end_time="
		SEC_USEC_FMT"\n", GET_SEC_USEC(bus->last_suspend_start_time),
		GET_SEC_USEC(bus->last_suspend_end_time),
		GET_SEC_USEC(bus->last_resume_start_time),
		GET_SEC_USEC(bus->last_resume_end_time));

#if defined(SHOW_LOGTRACE) && defined(DHD_USE_KTHREAD_FOR_LOGTRACE)
		bcm_bprintf(strbuf, "logtrace_thread_entry_time="SEC_USEC_FMT
			" logtrace_thread_sem_down_time="SEC_USEC_FMT
			"\nlogtrace_thread_flush_time="SEC_USEC_FMT
			" logtrace_thread_unexpected_break_time="SEC_USEC_FMT
			"\nlogtrace_thread_complete_time="SEC_USEC_FMT"\n",
			GET_SEC_USEC(dhd->logtrace_thr_ts.entry_time),
			GET_SEC_USEC(dhd->logtrace_thr_ts.sem_down_time),
			GET_SEC_USEC(dhd->logtrace_thr_ts.flush_time),
			GET_SEC_USEC(dhd->logtrace_thr_ts.unexpected_break_time),
			GET_SEC_USEC(dhd->logtrace_thr_ts.complete_time));
#endif /* SHOW_LOGTRACE && DHD_USE_KTHREAD_FOR_LOGTRACE */
}

void dhd_dump_intr_registers(dhd_pub_t *dhd, struct bcmstrbuf *strbuf)
{
	uint32 intstatus = 0;
	uint32 intmask = 0;
	uint32 d2h_db0 = 0;
	uint32 d2h_mb_data = 0;

	intstatus = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
		dhd->bus->pcie_mailbox_int, 0, 0);
	intmask = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
		dhd->bus->pcie_mailbox_mask, 0, 0);
	d2h_db0 = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, PCID2H_MailBox, 0, 0);
	dhd_bus_cmn_readshared(dhd->bus, &d2h_mb_data, D2H_MB_DATA, 0);

	bcm_bprintf(strbuf, "intstatus=0x%x intmask=0x%x d2h_db0=0x%x\n",
		intstatus, intmask, d2h_db0);
	bcm_bprintf(strbuf, "d2h_mb_data=0x%x def_intmask=0x%x\n",
		d2h_mb_data, dhd->bus->def_intmask);
}
/** Add bus dump output to a buffer */
void dhd_bus_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	uint16 flowid;
	int ix = 0;
	flow_ring_node_t *flow_ring_node;
	flow_info_t *flow_info;
#ifdef TX_STATUS_LATENCY_STATS
	uint8 ifindex;
	if_flow_lkup_t *if_flow_lkup;
	dhd_if_tx_status_latency_t if_tx_status_latency[DHD_MAX_IFS];
#endif /* TX_STATUS_LATENCY_STATS */

	if (dhdp->busstate != DHD_BUS_DATA)
		return;

#ifdef TX_STATUS_LATENCY_STATS
	memset(if_tx_status_latency, 0, sizeof(if_tx_status_latency));
#endif /* TX_STATUS_LATENCY_STATS */
#ifdef DHD_WAKE_STATUS
	bcm_bprintf(strbuf, "wake %u rxwake %u readctrlwake %u\n",
		bcmpcie_get_total_wake(dhdp->bus), dhdp->bus->wake_counts.rxwake,
		dhdp->bus->wake_counts.rcwake);
#ifdef DHD_WAKE_RX_STATUS
	bcm_bprintf(strbuf, " unicast %u muticast %u broadcast %u arp %u\n",
		dhdp->bus->wake_counts.rx_ucast, dhdp->bus->wake_counts.rx_mcast,
		dhdp->bus->wake_counts.rx_bcast, dhdp->bus->wake_counts.rx_arp);
	bcm_bprintf(strbuf, " multi4 %u multi6 %u icmp6 %u multiother %u\n",
		dhdp->bus->wake_counts.rx_multi_ipv4, dhdp->bus->wake_counts.rx_multi_ipv6,
		dhdp->bus->wake_counts.rx_icmpv6, dhdp->bus->wake_counts.rx_multi_other);
	bcm_bprintf(strbuf, " icmp6_ra %u, icmp6_na %u, icmp6_ns %u\n",
		dhdp->bus->wake_counts.rx_icmpv6_ra, dhdp->bus->wake_counts.rx_icmpv6_na,
		dhdp->bus->wake_counts.rx_icmpv6_ns);
#endif /* DHD_WAKE_RX_STATUS */
#ifdef DHD_WAKE_EVENT_STATUS
	for (flowid = 0; flowid < WLC_E_LAST; flowid++)
		if (dhdp->bus->wake_counts.rc_event[flowid] != 0)
			bcm_bprintf(strbuf, " %s = %u\n", bcmevent_get_name(flowid),
				dhdp->bus->wake_counts.rc_event[flowid]);
	bcm_bprintf(strbuf, "\n");
#endif /* DHD_WAKE_EVENT_STATUS */
#endif /* DHD_WAKE_STATUS */

	dhd_prot_print_info(dhdp, strbuf);
	dhd_dump_intr_registers(dhdp, strbuf);
	dhd_dump_intr_counters(dhdp, strbuf);
	bcm_bprintf(strbuf, "h2d_mb_data_ptr_addr 0x%x, d2h_mb_data_ptr_addr 0x%x\n",
		dhdp->bus->h2d_mb_data_ptr_addr, dhdp->bus->d2h_mb_data_ptr_addr);
	bcm_bprintf(strbuf, "dhd cumm_ctr %d\n", DHD_CUMM_CTR_READ(&dhdp->cumm_ctr));
#ifdef DHD_LIMIT_MULTI_CLIENT_FLOWRINGS
	bcm_bprintf(strbuf, "multi_client_flow_rings:%d max_multi_client_flow_rings:%d\n",
		dhdp->multi_client_flow_rings, dhdp->max_multi_client_flow_rings);
#endif /* DHD_LIMIT_MULTI_CLIENT_FLOWRINGS */
	bcm_bprintf(strbuf,
		"%4s %4s %2s %4s %17s %4s %4s %6s %10s %4s %4s ",
		"Num:", "Flow", "If", "Prio", ":Dest_MacAddress:", "Qlen", "CLen", "L2CLen",
		" Overflows", "  RD", "  WR");

#ifdef TX_STATUS_LATENCY_STATS
	/* Average Tx status/Completion Latency in micro secs */
	bcm_bprintf(strbuf, "%16s %16s ", "       NumTxPkts", "    AvgTxCmpL_Us");
#endif /* TX_STATUS_LATENCY_STATS */

	bcm_bprintf(strbuf, "\n");

	for (flowid = 0; flowid < dhdp->num_flow_rings; flowid++) {
		flow_ring_node = DHD_FLOW_RING(dhdp, flowid);
		if (!flow_ring_node->active)
			continue;

		flow_info = &flow_ring_node->flow_info;
		bcm_bprintf(strbuf,
			"%4d %4d %2d %4d "MACDBG" %4d %4d %6d %10u ", ix++,
			flow_ring_node->flowid, flow_info->ifindex, flow_info->tid,
			MAC2STRDBG(flow_info->da),
			DHD_FLOW_QUEUE_LEN(&flow_ring_node->queue),
			DHD_CUMM_CTR_READ(DHD_FLOW_QUEUE_CLEN_PTR(&flow_ring_node->queue)),
			DHD_CUMM_CTR_READ(DHD_FLOW_QUEUE_L2CLEN_PTR(&flow_ring_node->queue)),
			DHD_FLOW_QUEUE_FAILURES(&flow_ring_node->queue));
		dhd_prot_print_flow_ring(dhdp, flow_ring_node->prot_info, strbuf,
			"%4d %4d ");

#ifdef TX_STATUS_LATENCY_STATS
		bcm_bprintf(strbuf, "%16d %16d ",
			flow_info->num_tx_pkts,
			flow_info->num_tx_status ?
			DIV_U64_BY_U64(flow_info->cum_tx_status_latency,
			flow_info->num_tx_status) : 0);

		ifindex = flow_info->ifindex;
		ASSERT(ifindex < DHD_MAX_IFS);
		if (ifindex < DHD_MAX_IFS) {
			if_tx_status_latency[ifindex].num_tx_status += flow_info->num_tx_status;
			if_tx_status_latency[ifindex].cum_tx_status_latency +=
				flow_info->cum_tx_status_latency;
		} else {
			DHD_ERROR(("%s: Bad IF index: %d associated with flowid: %d\n",
				__FUNCTION__, ifindex, flowid));
		}
#endif /* TX_STATUS_LATENCY_STATS */
		bcm_bprintf(strbuf, "\n");
	}

#ifdef TX_STATUS_LATENCY_STATS
	bcm_bprintf(strbuf, "\n%s  %16s  %16s\n", "If", "AvgTxCmpL_Us", "NumTxStatus");
	if_flow_lkup = (if_flow_lkup_t *)dhdp->if_flow_lkup;
	for (ix = 0; ix < DHD_MAX_IFS; ix++) {
		if (!if_flow_lkup[ix].status) {
			continue;
		}
		bcm_bprintf(strbuf, "%2d  %16d  %16d\n",
			ix,
			if_tx_status_latency[ix].num_tx_status ?
			DIV_U64_BY_U64(if_tx_status_latency[ix].cum_tx_status_latency,
			if_tx_status_latency[ix].num_tx_status): 0,
			if_tx_status_latency[ix].num_tx_status);
	}
#endif /* TX_STATUS_LATENCY_STATS */

#ifdef DHD_HP2P
	if (dhdp->hp2p_capable) {
		bcm_bprintf(strbuf, "\n%s  %16s  %16s", "Flowid", "Tx_t0", "Tx_t1");

		for (flowid = 0; flowid < MAX_HP2P_FLOWS; flowid++) {
			hp2p_info_t *hp2p_info;
			int bin;

			hp2p_info = &dhdp->hp2p_info[flowid];
			if (hp2p_info->num_timer_start == 0)
				continue;

			bcm_bprintf(strbuf, "\n%d", hp2p_info->flowid);
			bcm_bprintf(strbuf, "\n%s", "Bin");

			for (bin = 0; bin < MAX_TX_HIST_BIN; bin++) {
				bcm_bprintf(strbuf, "\n%2d %20d  %16d", bin,
					hp2p_info->tx_t0[bin], hp2p_info->tx_t1[bin]);
			}

			bcm_bprintf(strbuf, "\n%s  %16s", "Flowid", "Rx_t0");
			bcm_bprintf(strbuf, "\n%d", hp2p_info->flowid);
			bcm_bprintf(strbuf, "\n%s", "Bin");

			for (bin = 0; bin < MAX_RX_HIST_BIN; bin++) {
				bcm_bprintf(strbuf, "\n%d %20d", bin,
					hp2p_info->rx_t0[bin]);
			}

			bcm_bprintf(strbuf, "\n%s  %16s  %16s",
				"Packet limit", "Timer limit", "Timer start");
			bcm_bprintf(strbuf, "\n%d %24d %16d", hp2p_info->num_pkt_limit,
				hp2p_info->num_timer_limit, hp2p_info->num_timer_start);
		}

		bcm_bprintf(strbuf, "\n");
	}
#endif /* DHD_HP2P */

	bcm_bprintf(strbuf, "D3 inform cnt %d\n", dhdp->bus->d3_inform_cnt);
	bcm_bprintf(strbuf, "D0 inform cnt %d\n", dhdp->bus->d0_inform_cnt);
	bcm_bprintf(strbuf, "D0 inform in use cnt %d\n", dhdp->bus->d0_inform_in_use_cnt);
	if (dhdp->d2h_hostrdy_supported) {
		bcm_bprintf(strbuf, "hostready count:%d\n", dhdp->bus->hostready_count);
	}
	bcm_bprintf(strbuf, "d2h_intr_method -> %s\n",
		dhdp->bus->d2h_intr_method ? "PCIE_MSI" : "PCIE_INTX");
}

#ifdef DNGL_AXI_ERROR_LOGGING
bool
dhd_axi_sig_match(dhd_pub_t *dhdp)
{
	uint32 axi_tcm_addr = dhdpcie_bus_rtcm32(dhdp->bus, dhdp->axierror_logbuf_addr);

	if (dhdp->dhd_induce_error == DHD_INDUCE_DROP_AXI_SIG) {
		DHD_ERROR(("%s: Induce AXI signature drop\n", __FUNCTION__));
		return FALSE;
	}

	DHD_ERROR(("%s: axi_tcm_addr: 0x%x, tcm range: 0x%x ~ 0x%x\n",
		__FUNCTION__, axi_tcm_addr, dhdp->bus->dongle_ram_base,
		dhdp->bus->dongle_ram_base + dhdp->bus->ramsize));
	if (axi_tcm_addr >= dhdp->bus->dongle_ram_base &&
	    axi_tcm_addr < dhdp->bus->dongle_ram_base + dhdp->bus->ramsize) {
		uint32 axi_signature = dhdpcie_bus_rtcm32(dhdp->bus, (axi_tcm_addr +
			OFFSETOF(hnd_ext_trap_axi_error_v1_t, signature)));
		if (axi_signature == HND_EXT_TRAP_AXIERROR_SIGNATURE) {
			return TRUE;
		} else {
			DHD_ERROR(("%s: No AXI signature: 0x%x\n",
				__FUNCTION__, axi_signature));
			return FALSE;
		}
	} else {
		DHD_ERROR(("%s: No AXI shared tcm address debug info.\n", __FUNCTION__));
		return FALSE;
	}
}

void
dhd_axi_error(dhd_pub_t *dhdp)
{
	dhd_axi_error_dump_t *axi_err_dump;
	uint8 *axi_err_buf = NULL;
	uint8 *p_axi_err = NULL;
	uint32 axi_logbuf_addr;
	uint32 axi_tcm_addr;
	int err, size;

	OSL_DELAY(75000);

	axi_logbuf_addr = dhdp->axierror_logbuf_addr;
	if (!axi_logbuf_addr) {
		DHD_ERROR(("%s: No AXI TCM address debug info.\n", __FUNCTION__));
		goto sched_axi;
	}

	axi_err_dump = dhdp->axi_err_dump;
	if (!axi_err_dump) {
		goto sched_axi;
	}

	if (!dhd_axi_sig_match(dhdp)) {
		goto sched_axi;
	}

	/* Reading AXI error data for SMMU fault */
	DHD_ERROR(("%s: Read AXI data from TCM address\n", __FUNCTION__));
	axi_tcm_addr = dhdpcie_bus_rtcm32(dhdp->bus, axi_logbuf_addr);
	size = sizeof(hnd_ext_trap_axi_error_v1_t);
	axi_err_buf = MALLOCZ(dhdp->osh, size);
	if (axi_err_buf == NULL) {
		DHD_ERROR(("%s: out of memory !\n", __FUNCTION__));
		goto sched_axi;
	}

	p_axi_err = axi_err_buf;
	err = dhdpcie_bus_membytes(dhdp->bus, FALSE, axi_tcm_addr, p_axi_err, size);
	if (err) {
		DHD_ERROR(("%s: error %d on reading %d membytes at 0x%08x\n",
			__FUNCTION__, err, size, axi_tcm_addr));
		goto sched_axi;
	}

	/* Dump data to Dmesg */
	dhd_log_dump_axi_error(axi_err_buf);
	err = memcpy_s(&axi_err_dump->etd_axi_error_v1, size, axi_err_buf, size);
	if (err) {
		DHD_ERROR(("%s: failed to copy etd axi error info, err=%d\n",
			__FUNCTION__, err));
	}

sched_axi:
	if (axi_err_buf) {
		MFREE(dhdp->osh, axi_err_buf, size);
	}
	dhd_schedule_axi_error_dump(dhdp, NULL);
}

static void
dhd_log_dump_axi_error(uint8 *axi_err)
{
	dma_dentry_v1_t dma_dentry;
	dma_fifo_v1_t dma_fifo;
	int i = 0, j = 0;

	if (*(uint8 *)axi_err == HND_EXT_TRAP_AXIERROR_VERSION_1) {
		hnd_ext_trap_axi_error_v1_t *axi_err_v1 = (hnd_ext_trap_axi_error_v1_t *)axi_err;
		DHD_ERROR(("%s: signature : 0x%x\n", __FUNCTION__, axi_err_v1->signature));
		DHD_ERROR(("%s: version : 0x%x\n", __FUNCTION__, axi_err_v1->version));
		DHD_ERROR(("%s: length : 0x%x\n", __FUNCTION__, axi_err_v1->length));
		DHD_ERROR(("%s: dma_fifo_valid_count : 0x%x\n",
			__FUNCTION__, axi_err_v1->dma_fifo_valid_count));
		DHD_ERROR(("%s: axi_errorlog_status : 0x%x\n",
			__FUNCTION__, axi_err_v1->axi_errorlog_status));
		DHD_ERROR(("%s: axi_errorlog_core : 0x%x\n",
			__FUNCTION__, axi_err_v1->axi_errorlog_core));
		DHD_ERROR(("%s: axi_errorlog_hi : 0x%x\n",
			__FUNCTION__, axi_err_v1->axi_errorlog_hi));
		DHD_ERROR(("%s: axi_errorlog_lo : 0x%x\n",
			__FUNCTION__, axi_err_v1->axi_errorlog_lo));
		DHD_ERROR(("%s: axi_errorlog_id : 0x%x\n",
			__FUNCTION__, axi_err_v1->axi_errorlog_id));

		for (i = 0; i < MAX_DMAFIFO_ENTRIES_V1; i++) {
			dma_fifo = axi_err_v1->dma_fifo[i];
			DHD_ERROR(("%s: valid:%d : 0x%x\n", __FUNCTION__, i, dma_fifo.valid));
			DHD_ERROR(("%s: direction:%d : 0x%x\n",
				__FUNCTION__, i, dma_fifo.direction));
			DHD_ERROR(("%s: index:%d : 0x%x\n",
				__FUNCTION__, i, dma_fifo.index));
			DHD_ERROR(("%s: dpa:%d : 0x%x\n",
				__FUNCTION__, i, dma_fifo.dpa));
			DHD_ERROR(("%s: desc_lo:%d : 0x%x\n",
				__FUNCTION__, i, dma_fifo.desc_lo));
			DHD_ERROR(("%s: desc_hi:%d : 0x%x\n",
				__FUNCTION__, i, dma_fifo.desc_hi));
			DHD_ERROR(("%s: din:%d : 0x%x\n",
				__FUNCTION__, i, dma_fifo.din));
			DHD_ERROR(("%s: dout:%d : 0x%x\n",
				__FUNCTION__, i, dma_fifo.dout));
			for (j = 0; j < MAX_DMAFIFO_DESC_ENTRIES_V1; j++) {
				dma_dentry = axi_err_v1->dma_fifo[i].dentry[j];
				DHD_ERROR(("%s: ctrl1:%d : 0x%x\n",
					__FUNCTION__, i, dma_dentry.ctrl1));
				DHD_ERROR(("%s: ctrl2:%d : 0x%x\n",
					__FUNCTION__, i, dma_dentry.ctrl2));
				DHD_ERROR(("%s: addrlo:%d : 0x%x\n",
					__FUNCTION__, i, dma_dentry.addrlo));
				DHD_ERROR(("%s: addrhi:%d : 0x%x\n",
					__FUNCTION__, i, dma_dentry.addrhi));
			}
		}
	}
	else {
		DHD_ERROR(("%s: Invalid AXI version: 0x%x\n", __FUNCTION__, (*(uint8 *)axi_err)));
	}
}
#endif /* DNGL_AXI_ERROR_LOGGING */

/**
 * Brings transmit packets on all flow rings closer to the dongle, by moving (a subset) from their
 * flow queue to their flow ring.
 */
static void
dhd_update_txflowrings(dhd_pub_t *dhd)
{
	unsigned long flags;
	dll_t *item, *next;
	flow_ring_node_t *flow_ring_node;
	struct dhd_bus *bus = dhd->bus;

	if (dhd_query_bus_erros(dhd)) {
		return;
	}

	/* Hold flowring_list_lock to ensure no race condition while accessing the List */
	DHD_FLOWRING_LIST_LOCK(bus->dhd->flowring_list_lock, flags);
	for (item = dll_head_p(&bus->flowring_active_list);
		(!dhd_is_device_removed(dhd) && !dll_end(&bus->flowring_active_list, item));
		item = next) {
		if (dhd->hang_was_sent) {
			break;
		}

		next = dll_next_p(item);
		flow_ring_node = dhd_constlist_to_flowring(item);

		/* Ensure that flow_ring_node in the list is Not Null */
		ASSERT(flow_ring_node != NULL);

		/* Ensure that the flowring node has valid contents */
		ASSERT(flow_ring_node->prot_info != NULL);

		dhd_prot_update_txflowring(dhd, flow_ring_node->flowid, flow_ring_node->prot_info);
	}
	DHD_FLOWRING_LIST_UNLOCK(bus->dhd->flowring_list_lock, flags);
}

/** Mailbox ringbell Function */
static void
dhd_bus_gen_devmb_intr(struct dhd_bus *bus)
{
	if ((bus->sih->buscorerev == 2) || (bus->sih->buscorerev == 6) ||
		(bus->sih->buscorerev == 4)) {
		DHD_ERROR(("mailbox communication not supported\n"));
		return;
	}
	if (bus->db1_for_mb)  {
		/* this is a pcie core register, not the config register */
		DHD_INFO(("writing a mail box interrupt to the device, through doorbell 1\n"));
		if (DAR_PWRREQ(bus)) {
			dhd_bus_pcie_pwr_req(bus);
		}
		si_corereg(bus->sih, bus->sih->buscoreidx, dhd_bus_db1_addr_get(bus),
			~0, 0x12345678);
	} else {
		DHD_INFO(("writing a mail box interrupt to the device, through config space\n"));
		dhdpcie_bus_cfg_write_dword(bus, PCISBMbx, 4, (1 << 0));
		dhdpcie_bus_cfg_write_dword(bus, PCISBMbx, 4, (1 << 0));
	}
}

/* Upon receiving a mailbox interrupt,
 * if H2D_FW_TRAP bit is set in mailbox location
 * device traps
 */
static void
dhdpcie_fw_trap(dhd_bus_t *bus)
{
	/* Send the mailbox data and generate mailbox intr. */
	dhdpcie_send_mb_data(bus, H2D_FW_TRAP);
	/* For FWs that cannot interprete H2D_FW_TRAP */
	(void)dhd_wl_ioctl_set_intiovar(bus->dhd, "bus:disconnect", 99, WLC_SET_VAR, TRUE, 0);
}

/** mailbox doorbell ring function */
void
dhd_bus_ringbell(struct dhd_bus *bus, uint32 value)
{
	/* Skip after sending D3_INFORM */
	if (bus->bus_low_power_state != DHD_BUS_NO_LOW_POWER_STATE) {
		DHD_ERROR(("%s: trying to ring the doorbell after D3 inform %d\n",
			__FUNCTION__, bus->bus_low_power_state));
		return;
	}

	/* Skip in the case of link down */
	if (bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link was down\n", __FUNCTION__));
		return;
	}

	if ((bus->sih->buscorerev == 2) || (bus->sih->buscorerev == 6) ||
		(bus->sih->buscorerev == 4)) {
		si_corereg(bus->sih, bus->sih->buscoreidx, bus->pcie_mailbox_int,
			PCIE_INTB, PCIE_INTB);
	} else {
		/* this is a pcie core register, not the config regsiter */
		DHD_INFO(("writing a door bell to the device\n"));
		if (IDMA_ACTIVE(bus->dhd)) {
			if (DAR_PWRREQ(bus)) {
				dhd_bus_pcie_pwr_req(bus);
			}
			si_corereg(bus->sih, bus->sih->buscoreidx, dhd_bus_db0_addr_2_get(bus),
				~0, value);
		} else {
			if (DAR_PWRREQ(bus)) {
				dhd_bus_pcie_pwr_req(bus);
			}
			si_corereg(bus->sih, bus->sih->buscoreidx,
				dhd_bus_db0_addr_get(bus), ~0, 0x12345678);
		}
	}
}

/** mailbox doorbell ring function for IDMA/IFRM using dma channel2 */
void
dhd_bus_ringbell_2(struct dhd_bus *bus, uint32 value, bool devwake)
{
	/* this is a pcie core register, not the config regsiter */
	/* Skip after sending D3_INFORM */
	if (bus->bus_low_power_state != DHD_BUS_NO_LOW_POWER_STATE) {
		DHD_ERROR(("%s: trying to ring the doorbell after D3 inform %d\n",
			__FUNCTION__, bus->bus_low_power_state));
		return;
	}

	/* Skip in the case of link down */
	if (bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link was down\n", __FUNCTION__));
		return;
	}

	DHD_INFO(("writing a door bell 2 to the device\n"));
	if (DAR_PWRREQ(bus)) {
		dhd_bus_pcie_pwr_req(bus);
	}
	si_corereg(bus->sih, bus->sih->buscoreidx, dhd_bus_db0_addr_2_get(bus),
		~0, value);
}

void
dhdpcie_bus_ringbell_fast(struct dhd_bus *bus, uint32 value)
{
	/* Skip after sending D3_INFORM */
	if (bus->bus_low_power_state != DHD_BUS_NO_LOW_POWER_STATE) {
		DHD_ERROR(("%s: trying to ring the doorbell after D3 inform %d\n",
			__FUNCTION__, bus->bus_low_power_state));
		return;
	}

	/* Skip in the case of link down */
	if (bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link was down\n", __FUNCTION__));
		return;
	}

	if (DAR_PWRREQ(bus)) {
		dhd_bus_pcie_pwr_req(bus);
	}

#ifdef DHD_DB0TS
	if (bus->dhd->db0ts_capable) {
		uint64 ts;

		ts = local_clock();
		do_div(ts, 1000);

		value = htol32(ts & 0xFFFFFFFF);
		DHD_INFO(("%s: usec timer = 0x%x\n", __FUNCTION__, value));
	}
#endif /* DHD_DB0TS */
	W_REG(bus->pcie_mb_intr_osh, bus->pcie_mb_intr_addr, value);
}

void
dhdpcie_bus_ringbell_2_fast(struct dhd_bus *bus, uint32 value, bool devwake)
{
	/* Skip after sending D3_INFORM */
	if (bus->bus_low_power_state != DHD_BUS_NO_LOW_POWER_STATE) {
		DHD_ERROR(("%s: trying to ring the doorbell after D3 inform %d\n",
			__FUNCTION__, bus->bus_low_power_state));
		return;
	}

	/* Skip in the case of link down */
	if (bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link was down\n", __FUNCTION__));
		return;
	}

	if (DAR_PWRREQ(bus)) {
		dhd_bus_pcie_pwr_req(bus);
	}
	W_REG(bus->pcie_mb_intr_osh, bus->pcie_mb_intr_2_addr, value);
}

static void
dhd_bus_ringbell_oldpcie(struct dhd_bus *bus, uint32 value)
{
	uint32 w;
	/* Skip after sending D3_INFORM */
	if (bus->bus_low_power_state != DHD_BUS_NO_LOW_POWER_STATE) {
		DHD_ERROR(("%s: trying to ring the doorbell after D3 inform %d\n",
			__FUNCTION__, bus->bus_low_power_state));
		return;
	}

	/* Skip in the case of link down */
	if (bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link was down\n", __FUNCTION__));
		return;
	}

	w = (R_REG(bus->pcie_mb_intr_osh, bus->pcie_mb_intr_addr) & ~PCIE_INTB) | PCIE_INTB;
	W_REG(bus->pcie_mb_intr_osh, bus->pcie_mb_intr_addr, w);
}

dhd_mb_ring_t
dhd_bus_get_mbintr_fn(struct dhd_bus *bus)
{
	if ((bus->sih->buscorerev == 2) || (bus->sih->buscorerev == 6) ||
		(bus->sih->buscorerev == 4)) {
		bus->pcie_mb_intr_addr = si_corereg_addr(bus->sih, bus->sih->buscoreidx,
			bus->pcie_mailbox_int);
		if (bus->pcie_mb_intr_addr) {
			bus->pcie_mb_intr_osh = si_osh(bus->sih);
			return dhd_bus_ringbell_oldpcie;
		}
	} else {
		bus->pcie_mb_intr_addr = si_corereg_addr(bus->sih, bus->sih->buscoreidx,
			dhd_bus_db0_addr_get(bus));
		if (bus->pcie_mb_intr_addr) {
			bus->pcie_mb_intr_osh = si_osh(bus->sih);
			return dhdpcie_bus_ringbell_fast;
		}
	}
	return dhd_bus_ringbell;
}

dhd_mb_ring_2_t
dhd_bus_get_mbintr_2_fn(struct dhd_bus *bus)
{
	bus->pcie_mb_intr_2_addr = si_corereg_addr(bus->sih, bus->sih->buscoreidx,
		dhd_bus_db0_addr_2_get(bus));
	if (bus->pcie_mb_intr_2_addr) {
		bus->pcie_mb_intr_osh = si_osh(bus->sih);
		return dhdpcie_bus_ringbell_2_fast;
	}
	return dhd_bus_ringbell_2;
}

bool BCMFASTPATH
dhd_bus_dpc(struct dhd_bus *bus)
{
	bool resched = FALSE;	  /* Flag indicating resched wanted */
	unsigned long flags;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	bus->dpc_entry_time = OSL_LOCALTIME_NS();

	DHD_GENERAL_LOCK(bus->dhd, flags);
	/* Check for only DHD_BUS_DOWN and not for DHD_BUS_DOWN_IN_PROGRESS
	 * to avoid IOCTL Resumed On timeout when ioctl is waiting for response
	 * and rmmod is fired in parallel, which will make DHD_BUS_DOWN_IN_PROGRESS
	 * and if we return from here, then IOCTL response will never be handled
	 */
	if (bus->dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s: Bus down, ret\n", __FUNCTION__));
		bus->intstatus = 0;
		DHD_GENERAL_UNLOCK(bus->dhd, flags);
		bus->dpc_return_busdown_count++;
		return 0;
	}
#ifdef DHD_PCIE_RUNTIMEPM
	bus->idlecount = 0;
#endif /* DHD_PCIE_RUNTIMEPM */
	DHD_BUS_BUSY_SET_IN_DPC(bus->dhd);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);

	resched = dhdpcie_bus_process_mailbox_intr(bus, bus->intstatus);
	if (!resched) {
		bus->intstatus = 0;
		bus->dpc_intr_enable_count++;
		/* For Linux, Macos etc (otherthan NDIS) enable back the host interrupts
		 * which has been disabled in the dhdpcie_bus_isr()
		 */
		 dhdpcie_enable_irq(bus); /* Enable back interrupt!! */
		bus->dpc_exit_time = OSL_LOCALTIME_NS();
	} else {
		bus->resched_dpc_time = OSL_LOCALTIME_NS();
	}

	bus->dpc_sched = resched;

	DHD_GENERAL_LOCK(bus->dhd, flags);
	DHD_BUS_BUSY_CLEAR_IN_DPC(bus->dhd);
	dhd_os_busbusy_wake(bus->dhd);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);

	return resched;

}

int
dhdpcie_send_mb_data(dhd_bus_t *bus, uint32 h2d_mb_data)
{
	uint32 cur_h2d_mb_data = 0;

	DHD_INFO_HW4(("%s: H2D_MB_DATA: 0x%08X\n", __FUNCTION__, h2d_mb_data));

	if (bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link was down\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (bus->api.fw_rev >= PCIE_SHARED_VERSION_6 && !bus->use_mailbox) {
		DHD_INFO(("API rev is 6, sending mb data as H2D Ctrl message to dongle, 0x%04x\n",
			h2d_mb_data));
		/* Prevent asserting device_wake during doorbell ring for mb data to avoid loop. */
		{
			if (dhd_prot_h2d_mbdata_send_ctrlmsg(bus->dhd, h2d_mb_data)) {
				DHD_ERROR(("failure sending the H2D Mailbox message "
					"to firmware\n"));
				goto fail;
			}
		}
		goto done;
	}

	dhd_bus_cmn_readshared(bus, &cur_h2d_mb_data, H2D_MB_DATA, 0);

	if (cur_h2d_mb_data != 0) {
		uint32 i = 0;
		DHD_INFO(("GRRRRRRR: MB transaction is already pending 0x%04x\n", cur_h2d_mb_data));
		while ((i++ < 100) && cur_h2d_mb_data) {
			OSL_DELAY(10);
			dhd_bus_cmn_readshared(bus, &cur_h2d_mb_data, H2D_MB_DATA, 0);
		}
		if (i >= 100) {
			DHD_ERROR(("%s : waited 1ms for the dngl "
				"to ack the previous mb transaction\n", __FUNCTION__));
			DHD_ERROR(("%s : MB transaction is still pending 0x%04x\n",
				__FUNCTION__, cur_h2d_mb_data));
		}
	}

	dhd_bus_cmn_writeshared(bus, &h2d_mb_data, sizeof(uint32), H2D_MB_DATA, 0);
	dhd_bus_gen_devmb_intr(bus);

done:
	if (h2d_mb_data == H2D_HOST_D3_INFORM) {
		DHD_INFO_HW4(("%s: send H2D_HOST_D3_INFORM to dongle\n", __FUNCTION__));
		bus->last_d3_inform_time = OSL_LOCALTIME_NS();
		bus->d3_inform_cnt++;
	}
	if (h2d_mb_data == H2D_HOST_D0_INFORM_IN_USE) {
		DHD_INFO_HW4(("%s: send H2D_HOST_D0_INFORM_IN_USE to dongle\n", __FUNCTION__));
		bus->d0_inform_in_use_cnt++;
	}
	if (h2d_mb_data == H2D_HOST_D0_INFORM) {
		DHD_INFO_HW4(("%s: send H2D_HOST_D0_INFORM to dongle\n", __FUNCTION__));
		bus->d0_inform_cnt++;
	}
	return BCME_OK;
fail:
	return BCME_ERROR;
}

static void
dhd_bus_handle_d3_ack(dhd_bus_t *bus)
{
	unsigned long flags_bus;
	DHD_BUS_LOCK(bus->bus_lock, flags_bus);
	bus->suspend_intr_disable_count++;
	/* Disable dongle Interrupts Immediately after D3 */

	/* For Linux, Macos etc (otherthan NDIS) along with disabling
	 * dongle interrupt by clearing the IntMask, disable directly
	 * interrupt from the host side as well. Also clear the intstatus
	 * if it is set to avoid unnecessary intrrupts after D3 ACK.
	 */
	dhdpcie_bus_intr_disable(bus); /* Disable interrupt using IntMask!! */
	dhdpcie_bus_clear_intstatus(bus);
	dhdpcie_disable_irq_nosync(bus); /* Disable host interrupt!! */

	if (bus->dhd->dhd_induce_error != DHD_INDUCE_D3_ACK_TIMEOUT) {
		/* Set bus_low_power_state to DHD_BUS_D3_ACK_RECIEVED */
		bus->bus_low_power_state = DHD_BUS_D3_ACK_RECIEVED;
		DHD_ERROR(("%s: D3_ACK Recieved\n", __FUNCTION__));
	}
	DHD_BUS_UNLOCK(bus->bus_lock, flags_bus);
	/* Check for D3 ACK induce flag, which is set by firing dhd iovar to induce D3 Ack timeout.
	 * If flag is set, D3 wake is skipped, which results in to D3 Ack timeout.
	 */
	if (bus->dhd->dhd_induce_error != DHD_INDUCE_D3_ACK_TIMEOUT) {
		bus->wait_for_d3_ack = 1;
		dhd_os_d3ack_wake(bus->dhd);
	} else {
		DHD_ERROR(("%s: Inducing D3 ACK timeout\n", __FUNCTION__));
	}
}
void
dhd_bus_handle_mb_data(dhd_bus_t *bus, uint32 d2h_mb_data)
{
	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req(bus);
	}

	DHD_INFO(("D2H_MB_DATA: 0x%04x\n", d2h_mb_data));

	if (d2h_mb_data & D2H_DEV_FWHALT) {
		DHD_ERROR(("FW trap has happened\n"));
		dhdpcie_checkdied(bus, NULL, 0);
#ifdef OEM_ANDROID
#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
		bus->no_cfg_restore = 1;
#endif /* CONFIG_ARCH_MSM */
#endif /* SUPPORT_LINKDOWN_RECOVERY */
		dhd_os_check_hang(bus->dhd, 0, -EREMOTEIO);
#endif /* OEM_ANDROID */
		goto exit;
	}
	if (d2h_mb_data & D2H_DEV_DS_ENTER_REQ)  {
		bool ds_acked = FALSE;
		BCM_REFERENCE(ds_acked);
		if (bus->bus_low_power_state == DHD_BUS_D3_ACK_RECIEVED) {
			DHD_ERROR(("DS-ENTRY AFTER D3-ACK!!!!! QUITING\n"));
			DHD_ERROR(("%s: making DHD_BUS_DOWN\n", __FUNCTION__));
			bus->dhd->busstate = DHD_BUS_DOWN;
			goto exit;
		}
		/* what should we do */
		DHD_INFO(("D2H_MB_DATA: DEEP SLEEP REQ\n"));
		{
			dhdpcie_send_mb_data(bus, H2D_HOST_DS_ACK);
			DHD_INFO(("D2H_MB_DATA: sent DEEP SLEEP ACK\n"));
		}
	}
	if (d2h_mb_data & D2H_DEV_DS_EXIT_NOTE)  {
		/* what should we do */
		DHD_INFO(("D2H_MB_DATA: DEEP SLEEP EXIT\n"));
	}
	if (d2h_mb_data & D2HMB_DS_HOST_SLEEP_EXIT_ACK)  {
		/* what should we do */
		DHD_INFO(("D2H_MB_DATA: D0 ACK\n"));
	}
	if (d2h_mb_data & D2H_DEV_D3_ACK)  {
		/* what should we do */
		DHD_INFO_HW4(("D2H_MB_DATA: D3 ACK\n"));
		if (!bus->wait_for_d3_ack) {
#if defined(DHD_HANG_SEND_UP_TEST)
			if (bus->dhd->req_hang_type == HANG_REASON_D3_ACK_TIMEOUT) {
				DHD_ERROR(("TEST HANG: Skip to process D3 ACK\n"));
			} else {
				dhd_bus_handle_d3_ack(bus);
			}
#else /* DHD_HANG_SEND_UP_TEST */
			dhd_bus_handle_d3_ack(bus);
#endif /* DHD_HANG_SEND_UP_TEST */
		}
	}

exit:
	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req_clear(bus);
	}
}

static void
dhdpcie_handle_mb_data(dhd_bus_t *bus)
{
	uint32 d2h_mb_data = 0;
	uint32 zero = 0;

	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req(bus);
	}

	dhd_bus_cmn_readshared(bus, &d2h_mb_data, D2H_MB_DATA, 0);
	if (D2H_DEV_MB_INVALIDATED(d2h_mb_data)) {
		DHD_ERROR(("%s: Invalid D2H_MB_DATA: 0x%08x\n",
			__FUNCTION__, d2h_mb_data));
		goto exit;
	}

	dhd_bus_cmn_writeshared(bus, &zero, sizeof(uint32), D2H_MB_DATA, 0);

	DHD_INFO_HW4(("D2H_MB_DATA: 0x%04x\n", d2h_mb_data));
	if (d2h_mb_data & D2H_DEV_FWHALT)  {
		DHD_ERROR(("FW trap has happened\n"));
		dhdpcie_checkdied(bus, NULL, 0);
		/* not ready yet dhd_os_ind_firmware_stall(bus->dhd); */
		goto exit;
	}
	if (d2h_mb_data & D2H_DEV_DS_ENTER_REQ)  {
		/* what should we do */
		DHD_INFO(("D2H_MB_DATA: DEEP SLEEP REQ\n"));
		dhdpcie_send_mb_data(bus, H2D_HOST_DS_ACK);
		DHD_INFO(("D2H_MB_DATA: sent DEEP SLEEP ACK\n"));
	}
	if (d2h_mb_data & D2H_DEV_DS_EXIT_NOTE)  {
		/* what should we do */
		DHD_INFO(("D2H_MB_DATA: DEEP SLEEP EXIT\n"));
	}
	if (d2h_mb_data & D2H_DEV_D3_ACK)  {
		/* what should we do */
		DHD_INFO_HW4(("D2H_MB_DATA: D3 ACK\n"));
		if (!bus->wait_for_d3_ack) {
#if defined(DHD_HANG_SEND_UP_TEST)
			if (bus->dhd->req_hang_type == HANG_REASON_D3_ACK_TIMEOUT) {
				DHD_ERROR(("TEST HANG: Skip to process D3 ACK\n"));
			} else {
			dhd_bus_handle_d3_ack(bus);
			}
#else /* DHD_HANG_SEND_UP_TEST */
			dhd_bus_handle_d3_ack(bus);
#endif /* DHD_HANG_SEND_UP_TEST */
		}
	}

exit:
	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req_clear(bus);
	}
}

static void
dhdpcie_read_handle_mb_data(dhd_bus_t *bus)
{
	uint32 d2h_mb_data = 0;
	uint32 zero = 0;

	if (bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link is down\n", __FUNCTION__));
		return;
	}

	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req(bus);
	}

	dhd_bus_cmn_readshared(bus, &d2h_mb_data, D2H_MB_DATA, 0);
	if (!d2h_mb_data) {
		goto exit;
	}

	dhd_bus_cmn_writeshared(bus, &zero, sizeof(uint32), D2H_MB_DATA, 0);

	dhd_bus_handle_mb_data(bus, d2h_mb_data);

exit:
	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req_clear(bus);
	}
}

static bool
dhdpcie_bus_process_mailbox_intr(dhd_bus_t *bus, uint32 intstatus)
{
	bool resched = FALSE;
	unsigned long flags_bus;

	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req(bus);
	}
	if ((bus->sih->buscorerev == 2) || (bus->sih->buscorerev == 6) ||
		(bus->sih->buscorerev == 4)) {
		/* Msg stream interrupt */
		if (intstatus & I_BIT1) {
			resched = dhdpci_bus_read_frames(bus);
		} else if (intstatus & I_BIT0) {
			/* do nothing for Now */
		}
	} else {
		if (intstatus & (PCIE_MB_TOPCIE_FN0_0 | PCIE_MB_TOPCIE_FN0_1))
			bus->api.handle_mb_data(bus);

		/* Do no process any rings after recieving D3_ACK */
		DHD_BUS_LOCK(bus->bus_lock, flags_bus);
		if (bus->bus_low_power_state == DHD_BUS_D3_ACK_RECIEVED) {
			DHD_ERROR(("%s: D3 Ack Recieved. "
				"Skip processing rest of ring buffers.\n", __FUNCTION__));
			DHD_BUS_UNLOCK(bus->bus_lock, flags_bus);
			goto exit;
		}
		DHD_BUS_UNLOCK(bus->bus_lock, flags_bus);

		/* Validate intstatus only for INTX case */
		if ((bus->d2h_intr_method == PCIE_MSI) ||
			((bus->d2h_intr_method == PCIE_INTX) && (intstatus & bus->d2h_mb_mask))) {
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
			if (pm_runtime_get(dhd_bus_to_dev(bus)) >= 0) {
				resched = dhdpci_bus_read_frames(bus);
				pm_runtime_mark_last_busy(dhd_bus_to_dev(bus));
				pm_runtime_put_autosuspend(dhd_bus_to_dev(bus));
			}
#else
			resched = dhdpci_bus_read_frames(bus);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
		}
	}

exit:
	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req_clear(bus);
	}
	return resched;
}

#if defined(DHD_H2D_LOG_TIME_SYNC)
static void
dhdpci_bus_rte_log_time_sync_poll(dhd_bus_t *bus)
{
	unsigned long time_elapsed;

	/* Poll for timeout value periodically */
	if ((bus->dhd->busstate == DHD_BUS_DATA) &&
		(bus->dhd->dhd_rte_time_sync_ms != 0) &&
		(bus->bus_low_power_state == DHD_BUS_NO_LOW_POWER_STATE)) {
		time_elapsed = OSL_SYSUPTIME_US() - bus->dhd_rte_time_sync_count;
		/* Compare time is milli seconds */
		if ((time_elapsed / 1000) >= bus->dhd->dhd_rte_time_sync_ms) {
			/*
			 * Its fine, if it has crossed the timeout value. No need to adjust the
			 * elapsed time
			 */
			bus->dhd_rte_time_sync_count += time_elapsed;

			/* Schedule deffered work. Work function will send IOVAR. */
			dhd_h2d_log_time_sync_deferred_wq_schedule(bus->dhd);
		}
	}
}
#endif /* DHD_H2D_LOG_TIME_SYNC */

static bool
dhdpci_bus_read_frames(dhd_bus_t *bus)
{
	bool more = FALSE;
	unsigned long flags_bus;

	/* First check if there a FW trap */
	if ((bus->api.fw_rev >= PCIE_SHARED_VERSION_6) &&
		(bus->dhd->dongle_trap_data = dhd_prot_process_trapbuf(bus->dhd))) {
#ifdef DNGL_AXI_ERROR_LOGGING
		if (bus->dhd->axi_error) {
			DHD_ERROR(("AXI Error happened\n"));
			return FALSE;
		}
#endif /* DNGL_AXI_ERROR_LOGGING */
		dhd_bus_handle_mb_data(bus, D2H_DEV_FWHALT);
		return FALSE;
	}

	/* There may be frames in both ctrl buf and data buf; check ctrl buf first */
	DHD_PERIM_LOCK_ALL((bus->dhd->fwder_unit % FWDER_MAX_UNIT));

	dhd_prot_process_ctrlbuf(bus->dhd);
	bus->last_process_ctrlbuf_time = OSL_LOCALTIME_NS();
	/* Unlock to give chance for resp to be handled */
	DHD_PERIM_UNLOCK_ALL((bus->dhd->fwder_unit % FWDER_MAX_UNIT));

	/* Do not process rest of ring buf once bus enters low power state (D3_INFORM/D3_ACK) */
	DHD_BUS_LOCK(bus->bus_lock, flags_bus);
	if (bus->bus_low_power_state != DHD_BUS_NO_LOW_POWER_STATE) {
		DHD_ERROR(("%s: Bus is in power save state (%d). "
			"Skip processing rest of ring buffers.\n",
			__FUNCTION__, bus->bus_low_power_state));
		DHD_BUS_UNLOCK(bus->bus_lock, flags_bus);
		return FALSE;
	}
	DHD_BUS_UNLOCK(bus->bus_lock, flags_bus);

	DHD_PERIM_LOCK_ALL((bus->dhd->fwder_unit % FWDER_MAX_UNIT));
	/* update the flow ring cpls */
	dhd_update_txflowrings(bus->dhd);
	bus->last_process_flowring_time = OSL_LOCALTIME_NS();

	/* With heavy TX traffic, we could get a lot of TxStatus
	 * so add bound
	 */
#ifdef DHD_HP2P
	more |= dhd_prot_process_msgbuf_txcpl(bus->dhd, dhd_txbound, DHD_HP2P_RING);
#endif /* DHD_HP2P */
	more |= dhd_prot_process_msgbuf_txcpl(bus->dhd, dhd_txbound, DHD_REGULAR_RING);
	bus->last_process_txcpl_time = OSL_LOCALTIME_NS();

	/* With heavy RX traffic, this routine potentially could spend some time
	 * processing RX frames without RX bound
	 */
#ifdef DHD_HP2P
	more |= dhd_prot_process_msgbuf_rxcpl(bus->dhd, dhd_rxbound, DHD_HP2P_RING);
#endif /* DHD_HP2P */
	more |= dhd_prot_process_msgbuf_rxcpl(bus->dhd, dhd_rxbound, DHD_REGULAR_RING);
	bus->last_process_rxcpl_time = OSL_LOCALTIME_NS();

	/* Process info ring completion messages */
#ifdef EWP_EDL
	if (!bus->dhd->dongle_edl_support)
#endif // endif
	{
		more |= dhd_prot_process_msgbuf_infocpl(bus->dhd, DHD_INFORING_BOUND);
		bus->last_process_infocpl_time = OSL_LOCALTIME_NS();
	}
#ifdef EWP_EDL
	else {
		more |= dhd_prot_process_msgbuf_edl(bus->dhd);
		bus->last_process_edl_time = OSL_LOCALTIME_NS();
	}
#endif /* EWP_EDL */

#ifdef IDLE_TX_FLOW_MGMT
	if (bus->enable_idle_flowring_mgmt) {
		/* Look for idle flow rings */
		dhd_bus_check_idle_scan(bus);
	}
#endif /* IDLE_TX_FLOW_MGMT */

	/* don't talk to the dongle if fw is about to be reloaded */
	if (bus->dhd->hang_was_sent) {
		more = FALSE;
	}
	DHD_PERIM_UNLOCK_ALL((bus->dhd->fwder_unit % FWDER_MAX_UNIT));

#ifdef SUPPORT_LINKDOWN_RECOVERY
	if (bus->read_shm_fail) {
		/* Read interrupt state once again to confirm linkdown */
		int intstatus = si_corereg(bus->sih, bus->sih->buscoreidx,
			bus->pcie_mailbox_int, 0, 0);
		if (intstatus != (uint32)-1) {
			DHD_ERROR(("%s: read SHM failed but intstatus is valid\n", __FUNCTION__));
#ifdef DHD_FW_COREDUMP
			if (bus->dhd->memdump_enabled) {
				DHD_OS_WAKE_LOCK(bus->dhd);
				bus->dhd->memdump_type = DUMP_TYPE_READ_SHM_FAIL;
				dhd_bus_mem_dump(bus->dhd);
				DHD_OS_WAKE_UNLOCK(bus->dhd);
			}
#endif /* DHD_FW_COREDUMP */
		} else {
			DHD_ERROR(("%s: Link is Down.\n", __FUNCTION__));
#ifdef CONFIG_ARCH_MSM
			bus->no_cfg_restore = 1;
#endif /* CONFIG_ARCH_MSM */
			bus->is_linkdown = 1;
		}

		dhd_prot_debug_info_print(bus->dhd);
		bus->dhd->hang_reason = HANG_REASON_PCIE_LINK_DOWN_EP_DETECT;
		dhd_os_send_hang_message(bus->dhd);
		more = FALSE;
	}
#endif /* SUPPORT_LINKDOWN_RECOVERY */
#if defined(DHD_H2D_LOG_TIME_SYNC)
	dhdpci_bus_rte_log_time_sync_poll(bus);
#endif /* DHD_H2D_LOG_TIME_SYNC */
	return more;
}

bool
dhdpcie_tcm_valid(dhd_bus_t *bus)
{
	uint32 addr = 0;
	int rv;
	uint32 shaddr = 0;
	pciedev_shared_t sh;

	shaddr = bus->dongle_ram_base + bus->ramsize - 4;

	/* Read last word in memory to determine address of pciedev_shared structure */
	addr = LTOH32(dhdpcie_bus_rtcm32(bus, shaddr));

	if ((addr == 0) || (addr == bus->nvram_csm) || (addr < bus->dongle_ram_base) ||
		(addr > shaddr)) {
		DHD_ERROR(("%s: address (0x%08x) of pciedev_shared invalid addr\n",
			__FUNCTION__, addr));
		return FALSE;
	}

	/* Read hndrte_shared structure */
	if ((rv = dhdpcie_bus_membytes(bus, FALSE, addr, (uint8 *)&sh,
		sizeof(pciedev_shared_t))) < 0) {
		DHD_ERROR(("Failed to read PCIe shared struct with %d\n", rv));
		return FALSE;
	}

	/* Compare any field in pciedev_shared_t */
	if (sh.console_addr != bus->pcie_sh->console_addr) {
		DHD_ERROR(("Contents of pciedev_shared_t structure are not matching.\n"));
		return FALSE;
	}

	return TRUE;
}

static void
dhdpcie_update_bus_api_revisions(uint32 firmware_api_version, uint32 host_api_version)
{
	snprintf(bus_api_revision, BUS_API_REV_STR_LEN, "\nBus API revisions:(FW rev%d)(DHD rev%d)",
			firmware_api_version, host_api_version);
	return;
}

static bool
dhdpcie_check_firmware_compatible(uint32 firmware_api_version, uint32 host_api_version)
{
	bool retcode = FALSE;

	DHD_INFO(("firmware api revision %d, host api revision %d\n",
		firmware_api_version, host_api_version));

	switch (firmware_api_version) {
	case PCIE_SHARED_VERSION_7:
	case PCIE_SHARED_VERSION_6:
	case PCIE_SHARED_VERSION_5:
		retcode = TRUE;
		break;
	default:
		if (firmware_api_version <= host_api_version)
			retcode = TRUE;
	}
	return retcode;
}

static int
dhdpcie_readshared_console(dhd_bus_t *bus)
{
	uint32 addr = 0;
	uint32 shaddr = 0;
	int rv;
	pciedev_shared_t *sh = bus->pcie_sh;
	dhd_timeout_t tmo;

	shaddr = bus->dongle_ram_base + bus->ramsize - 4;
	/* start a timer for 5 seconds */
	dhd_timeout_start(&tmo, MAX_READ_TIMEOUT);

	while (((addr == 0) || (addr == bus->nvram_csm)) && !dhd_timeout_expired(&tmo)) {
		/* Read last word in memory to determine address of pciedev_shared structure */
		addr = LTOH32(dhdpcie_bus_rtcm32(bus, shaddr));
	}

	if ((addr == 0) || (addr == bus->nvram_csm) || (addr < bus->dongle_ram_base) ||
		(addr > shaddr)) {
		DHD_ERROR(("%s: address (0x%08x) of pciedev_shared invalid\n",
			__FUNCTION__, addr));
		DHD_ERROR(("Waited %u usec, dongle is not ready\n", tmo.elapsed));
		return BCME_ERROR;
	} else {
		bus->shared_addr = (ulong)addr;
		DHD_ERROR(("%s:PCIe shared addr (0x%08x) read took %u usec "
			"before dongle is ready\n", __FUNCTION__, addr, tmo.elapsed));
	}

	/* Read hndrte_shared structure */
	if ((rv = dhdpcie_bus_membytes(bus, FALSE, addr, (uint8 *)sh,
		sizeof(pciedev_shared_t))) < 0) {
		DHD_ERROR(("Failed to read PCIe shared struct with %d\n", rv));
		return rv;
	}

	/* Endianness */
	sh->console_addr = ltoh32(sh->console_addr);
	/* load bus console address */
	bus->console_addr = sh->console_addr;

	return BCME_OK;
} /* dhdpcie_readshared_console */

static int
dhdpcie_readshared(dhd_bus_t *bus)
{
	uint32 addr = 0;
	int rv, dma_indx_wr_buf, dma_indx_rd_buf;
	uint32 shaddr = 0;
	pciedev_shared_t *sh = bus->pcie_sh;
	dhd_timeout_t tmo;
	bool idma_en = FALSE;

	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req(bus);
	}

	shaddr = bus->dongle_ram_base + bus->ramsize - 4;
	/* start a timer for 5 seconds */
	dhd_timeout_start(&tmo, MAX_READ_TIMEOUT);

	while (((addr == 0) || (addr == bus->nvram_csm)) && !dhd_timeout_expired(&tmo)) {
		/* Read last word in memory to determine address of pciedev_shared structure */
		addr = LTOH32(dhdpcie_bus_rtcm32(bus, shaddr));
	}

	if (addr == (uint32)-1) {
		DHD_ERROR(("%s: PCIe link might be down\n", __FUNCTION__));
#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
		bus->no_cfg_restore = 1;
#endif /* CONFIG_ARCH_MSM */
#endif /* SUPPORT_LINKDOWN_RECOVERY */
		bus->is_linkdown = 1;
		return BCME_ERROR;
	}

	if ((addr == 0) || (addr == bus->nvram_csm) || (addr < bus->dongle_ram_base) ||
		(addr > shaddr)) {
		DHD_ERROR(("%s: address (0x%08x) of pciedev_shared invalid\n",
			__FUNCTION__, addr));
		DHD_ERROR(("Waited %u usec, dongle is not ready\n", tmo.elapsed));
#ifdef DEBUG_DNGL_INIT_FAIL
		if (addr != (uint32)-1) {	/* skip further PCIE reads if read this addr */
#ifdef CUSTOMER_HW4_DEBUG
			bus->dhd->memdump_enabled = DUMP_MEMFILE_BUGON;
#endif /* CUSTOMER_HW4_DEBUG */
			if (bus->dhd->memdump_enabled) {
				bus->dhd->memdump_type = DUMP_TYPE_DONGLE_INIT_FAILURE;
				dhdpcie_mem_dump(bus);
			}
		}
#endif /* DEBUG_DNGL_INIT_FAIL */
		return BCME_ERROR;
	} else {
		bus->shared_addr = (ulong)addr;
		DHD_ERROR(("PCIe shared addr (0x%08x) read took %u usec "
			"before dongle is ready\n", addr, tmo.elapsed));
	}

	/* Read hndrte_shared structure */
	if ((rv = dhdpcie_bus_membytes(bus, FALSE, addr, (uint8 *)sh,
		sizeof(pciedev_shared_t))) < 0) {
		DHD_ERROR(("Failed to read PCIe shared struct with %d\n", rv));
		return rv;
	}

	/* Endianness */
	sh->flags = ltoh32(sh->flags);
	sh->trap_addr = ltoh32(sh->trap_addr);
	sh->assert_exp_addr = ltoh32(sh->assert_exp_addr);
	sh->assert_file_addr = ltoh32(sh->assert_file_addr);
	sh->assert_line = ltoh32(sh->assert_line);
	sh->console_addr = ltoh32(sh->console_addr);
	sh->msgtrace_addr = ltoh32(sh->msgtrace_addr);
	sh->dma_rxoffset = ltoh32(sh->dma_rxoffset);
	sh->rings_info_ptr = ltoh32(sh->rings_info_ptr);
	sh->flags2 = ltoh32(sh->flags2);

	/* load bus console address */
	bus->console_addr = sh->console_addr;

	/* Read the dma rx offset */
	bus->dma_rxoffset = bus->pcie_sh->dma_rxoffset;
	dhd_prot_rx_dataoffset(bus->dhd, bus->dma_rxoffset);

	DHD_INFO(("DMA RX offset from shared Area %d\n", bus->dma_rxoffset));

	bus->api.fw_rev = sh->flags & PCIE_SHARED_VERSION_MASK;
	if (!(dhdpcie_check_firmware_compatible(bus->api.fw_rev, PCIE_SHARED_VERSION)))
	{
		DHD_ERROR(("%s: pcie_shared version %d in dhd "
		           "is older than pciedev_shared version %d in dongle\n",
		           __FUNCTION__, PCIE_SHARED_VERSION,
		           bus->api.fw_rev));
		return BCME_ERROR;
	}
	dhdpcie_update_bus_api_revisions(bus->api.fw_rev, PCIE_SHARED_VERSION);

	bus->rw_index_sz = (sh->flags & PCIE_SHARED_2BYTE_INDICES) ?
		sizeof(uint16) : sizeof(uint32);
	DHD_INFO(("%s: Dongle advertizes %d size indices\n",
		__FUNCTION__, bus->rw_index_sz));

#ifdef IDLE_TX_FLOW_MGMT
	if (sh->flags & PCIE_SHARED_IDLE_FLOW_RING) {
		DHD_ERROR(("%s: FW Supports IdleFlow ring managment!\n",
			__FUNCTION__));
		bus->enable_idle_flowring_mgmt = TRUE;
	}
#endif /* IDLE_TX_FLOW_MGMT */

	if (IDMA_CAPABLE(bus)) {
		if (bus->sih->buscorerev == 23) {
		} else {
			idma_en = TRUE;
		}
	}

	/* TODO: This need to be selected based on IPC instead of compile time */
	bus->dhd->hwa_enable = TRUE;

	if (idma_en) {
		bus->dhd->idma_enable = (sh->flags & PCIE_SHARED_IDMA) ? TRUE : FALSE;
		bus->dhd->ifrm_enable = (sh->flags & PCIE_SHARED_IFRM) ? TRUE : FALSE;
	}

	bus->dhd->d2h_sync_mode = sh->flags & PCIE_SHARED_D2H_SYNC_MODE_MASK;

	bus->dhd->dar_enable = (sh->flags & PCIE_SHARED_DAR) ? TRUE : FALSE;

	/* Does the FW support DMA'ing r/w indices */
	if (sh->flags & PCIE_SHARED_DMA_INDEX) {
		if (!bus->dhd->dma_ring_upd_overwrite) {
			{
				if (!IFRM_ENAB(bus->dhd)) {
					bus->dhd->dma_h2d_ring_upd_support = TRUE;
				}
				bus->dhd->dma_d2h_ring_upd_support = TRUE;
			}
		}

		if (bus->dhd->dma_d2h_ring_upd_support)
			bus->dhd->d2h_sync_mode = 0;

		DHD_INFO(("%s: Host support DMAing indices: H2D:%d - D2H:%d. FW supports it\n",
			__FUNCTION__,
			(bus->dhd->dma_h2d_ring_upd_support ? 1 : 0),
			(bus->dhd->dma_d2h_ring_upd_support ? 1 : 0)));
	} else if (!(sh->flags & PCIE_SHARED_D2H_SYNC_MODE_MASK)) {
		DHD_ERROR(("%s FW has to support either dma indices or d2h sync\n",
			__FUNCTION__));
		return BCME_UNSUPPORTED;
	} else {
		bus->dhd->dma_h2d_ring_upd_support = FALSE;
		bus->dhd->dma_d2h_ring_upd_support = FALSE;
	}

	/* Does the firmware support fast delete ring? */
	if (sh->flags2 & PCIE_SHARED2_FAST_DELETE_RING) {
		DHD_INFO(("%s: Firmware supports fast delete ring\n",
			__FUNCTION__));
		bus->dhd->fast_delete_ring_support = TRUE;
	} else {
		DHD_INFO(("%s: Firmware does not support fast delete ring\n",
			__FUNCTION__));
		bus->dhd->fast_delete_ring_support = FALSE;
	}

	/* get ring_info, ring_state and mb data ptrs and store the addresses in bus structure */
	{
		ring_info_t  ring_info;

		/* boundary check */
		if (sh->rings_info_ptr > shaddr) {
			DHD_ERROR(("%s: rings_info_ptr is invalid 0x%x, skip reading ring info\n",
				__FUNCTION__, sh->rings_info_ptr));
			return BCME_ERROR;
		}

		if ((rv = dhdpcie_bus_membytes(bus, FALSE, sh->rings_info_ptr,
			(uint8 *)&ring_info, sizeof(ring_info_t))) < 0)
			return rv;

		bus->h2d_mb_data_ptr_addr = ltoh32(sh->h2d_mb_data_ptr);
		bus->d2h_mb_data_ptr_addr = ltoh32(sh->d2h_mb_data_ptr);

		if (bus->api.fw_rev >= PCIE_SHARED_VERSION_6) {
			bus->max_tx_flowrings = ltoh16(ring_info.max_tx_flowrings);
			bus->max_submission_rings = ltoh16(ring_info.max_submission_queues);
			bus->max_completion_rings = ltoh16(ring_info.max_completion_rings);
			bus->max_cmn_rings = bus->max_submission_rings - bus->max_tx_flowrings;
			bus->api.handle_mb_data = dhdpcie_read_handle_mb_data;
			bus->use_mailbox = sh->flags & PCIE_SHARED_USE_MAILBOX;
		}
		else {
			bus->max_tx_flowrings = ltoh16(ring_info.max_tx_flowrings);
			bus->max_submission_rings = bus->max_tx_flowrings;
			bus->max_completion_rings = BCMPCIE_D2H_COMMON_MSGRINGS;
			bus->max_cmn_rings = BCMPCIE_H2D_COMMON_MSGRINGS;
			bus->api.handle_mb_data = dhdpcie_handle_mb_data;
			bus->use_mailbox = TRUE;
		}
		if (bus->max_completion_rings == 0) {
			DHD_ERROR(("dongle completion rings are invalid %d\n",
				bus->max_completion_rings));
			return BCME_ERROR;
		}
		if (bus->max_submission_rings == 0) {
			DHD_ERROR(("dongle submission rings are invalid %d\n",
				bus->max_submission_rings));
			return BCME_ERROR;
		}
		if (bus->max_tx_flowrings == 0) {
			DHD_ERROR(("dongle txflow rings are invalid %d\n", bus->max_tx_flowrings));
			return BCME_ERROR;
		}

		/* If both FW and Host support DMA'ing indices, allocate memory and notify FW
		 * The max_sub_queues is read from FW initialized ring_info
		 */
		if (bus->dhd->dma_h2d_ring_upd_support || IDMA_ENAB(bus->dhd)) {
			dma_indx_wr_buf = dhd_prot_dma_indx_init(bus->dhd, bus->rw_index_sz,
				H2D_DMA_INDX_WR_BUF, bus->max_submission_rings);
			dma_indx_rd_buf = dhd_prot_dma_indx_init(bus->dhd, bus->rw_index_sz,
				D2H_DMA_INDX_RD_BUF, bus->max_completion_rings);

			if ((dma_indx_wr_buf != BCME_OK) || (dma_indx_rd_buf != BCME_OK)) {
				DHD_ERROR(("%s: Failed to allocate memory for dma'ing h2d indices"
						"Host will use w/r indices in TCM\n",
						__FUNCTION__));
				bus->dhd->dma_h2d_ring_upd_support = FALSE;
				bus->dhd->idma_enable = FALSE;
			}
		}

		if (bus->dhd->dma_d2h_ring_upd_support) {
			dma_indx_wr_buf = dhd_prot_dma_indx_init(bus->dhd, bus->rw_index_sz,
				D2H_DMA_INDX_WR_BUF, bus->max_completion_rings);
			dma_indx_rd_buf = dhd_prot_dma_indx_init(bus->dhd, bus->rw_index_sz,
				H2D_DMA_INDX_RD_BUF, bus->max_submission_rings);

			if ((dma_indx_wr_buf != BCME_OK) || (dma_indx_rd_buf != BCME_OK)) {
				DHD_ERROR(("%s: Failed to allocate memory for dma'ing d2h indices"
						"Host will use w/r indices in TCM\n",
						__FUNCTION__));
				bus->dhd->dma_d2h_ring_upd_support = FALSE;
			}
		}

		if (IFRM_ENAB(bus->dhd)) {
			dma_indx_wr_buf = dhd_prot_dma_indx_init(bus->dhd, bus->rw_index_sz,
				H2D_IFRM_INDX_WR_BUF, bus->max_tx_flowrings);

			if (dma_indx_wr_buf != BCME_OK) {
				DHD_ERROR(("%s: Failed to alloc memory for Implicit DMA\n",
						__FUNCTION__));
				bus->dhd->ifrm_enable = FALSE;
			}
		}

		/* read ringmem and ringstate ptrs from shared area and store in host variables */
		dhd_fillup_ring_sharedptr_info(bus, &ring_info);
		if (dhd_msg_level & DHD_INFO_VAL) {
			bcm_print_bytes("ring_info_raw", (uchar *)&ring_info, sizeof(ring_info_t));
		}
		DHD_INFO(("ring_info\n"));

		DHD_ERROR(("%s: max H2D queues %d\n",
			__FUNCTION__, ltoh16(ring_info.max_tx_flowrings)));

		DHD_INFO(("mail box address\n"));
		DHD_INFO(("%s: h2d_mb_data_ptr_addr 0x%04x\n",
			__FUNCTION__, bus->h2d_mb_data_ptr_addr));
		DHD_INFO(("%s: d2h_mb_data_ptr_addr 0x%04x\n",
			__FUNCTION__, bus->d2h_mb_data_ptr_addr));
	}

	DHD_INFO(("%s: d2h_sync_mode 0x%08x\n",
		__FUNCTION__, bus->dhd->d2h_sync_mode));

	bus->dhd->d2h_hostrdy_supported =
		((sh->flags & PCIE_SHARED_HOSTRDY_SUPPORT) == PCIE_SHARED_HOSTRDY_SUPPORT);

	bus->dhd->ext_trap_data_supported =
		((sh->flags2 & PCIE_SHARED2_EXTENDED_TRAP_DATA) == PCIE_SHARED2_EXTENDED_TRAP_DATA);

	if ((sh->flags2 & PCIE_SHARED2_TXSTATUS_METADATA) == 0)
		bus->dhd->pcie_txs_metadata_enable = 0;

	bus->dhd->hscb_enable =
		(sh->flags2 & PCIE_SHARED2_HSCB) == PCIE_SHARED2_HSCB;

#ifdef EWP_EDL
	if (host_edl_support) {
		bus->dhd->dongle_edl_support = (sh->flags2 & PCIE_SHARED2_EDL_RING) ? TRUE : FALSE;
		DHD_ERROR(("Dongle EDL support: %u\n", bus->dhd->dongle_edl_support));
	}
#endif /* EWP_EDL */

	bus->dhd->debug_buf_dest_support =
		(sh->flags2 & PCIE_SHARED2_DEBUG_BUF_DEST) ? TRUE : FALSE;
	DHD_ERROR(("FW supports debug buf dest ? %s \n",
		bus->dhd->debug_buf_dest_support ? "Y" : "N"));

#ifdef DHD_HP2P
	if (bus->dhd->hp2p_enable) {
		bus->dhd->hp2p_ts_capable =
			(sh->flags2 & PCIE_SHARED2_PKT_TIMESTAMP) == PCIE_SHARED2_PKT_TIMESTAMP;
		bus->dhd->hp2p_capable =
			(sh->flags2 & PCIE_SHARED2_HP2P) == PCIE_SHARED2_HP2P;
		bus->dhd->hp2p_capable &= bus->dhd->hp2p_ts_capable;

		DHD_ERROR(("FW supports HP2P ? %s \n",
			bus->dhd->hp2p_capable ? "Y" : "N"));

		if (bus->dhd->hp2p_capable) {
			bus->dhd->pkt_thresh = HP2P_PKT_THRESH;
			bus->dhd->pkt_expiry = HP2P_PKT_EXPIRY;
			bus->dhd->time_thresh = HP2P_TIME_THRESH;
			for (addr = 0; addr < MAX_HP2P_FLOWS; addr++) {
				hp2p_info_t *hp2p_info = &bus->dhd->hp2p_info[addr];

				hp2p_info->hrtimer_init = FALSE;
				hp2p_info->timer.function = &dhd_hp2p_write;
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(5, 1, 21))
				tasklet_hrtimer_init(&hp2p_info->timer,
					dhd_hp2p_write, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
#else
				hrtimer_init(&hp2p_info->timer, CLOCK_MONOTONIC,
					HRTIMER_MODE_REL_SOFT);
#endif /* LINUX_VERSION_CODE <= KERNEL_VERSION(5, 1, 21 */
			}
		}
	}
#endif /* DHD_HP2P */

#ifdef DHD_DB0TS
	bus->dhd->db0ts_capable =
		(sh->flags & PCIE_SHARED_TIMESTAMP_DB0) == PCIE_SHARED_TIMESTAMP_DB0;
#endif /* DHD_DB0TS */

	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req_clear(bus);

		/*
		 * WAR to fix ARM cold boot;
		 * De-assert WL domain in DAR
		 */
		if (bus->sih->buscorerev >= 68) {
			dhd_bus_pcie_pwr_req_wl_domain(bus, FALSE);
		}
	}
	return BCME_OK;
} /* dhdpcie_readshared */

/** Read ring mem and ring state ptr info from shared memory area in device memory */
static void
dhd_fillup_ring_sharedptr_info(dhd_bus_t *bus, ring_info_t *ring_info)
{
	uint16 i = 0;
	uint16 j = 0;
	uint32 tcm_memloc;
	uint32	d2h_w_idx_ptr, d2h_r_idx_ptr, h2d_w_idx_ptr, h2d_r_idx_ptr;
	uint16  max_tx_flowrings = bus->max_tx_flowrings;

	/* Ring mem ptr info */
	/* Alloated in the order
		H2D_MSGRING_CONTROL_SUBMIT              0
		H2D_MSGRING_RXPOST_SUBMIT               1
		D2H_MSGRING_CONTROL_COMPLETE            2
		D2H_MSGRING_TX_COMPLETE                 3
		D2H_MSGRING_RX_COMPLETE                 4
	*/

	{
		/* ringmemptr holds start of the mem block address space */
		tcm_memloc = ltoh32(ring_info->ringmem_ptr);

		/* Find out ringmem ptr for each ring common  ring */
		for (i = 0; i <= BCMPCIE_COMMON_MSGRING_MAX_ID; i++) {
			bus->ring_sh[i].ring_mem_addr = tcm_memloc;
			/* Update mem block */
			tcm_memloc = tcm_memloc + sizeof(ring_mem_t);
			DHD_INFO(("ring id %d ring mem addr 0x%04x \n",
				i, bus->ring_sh[i].ring_mem_addr));
		}
	}

	/* Ring state mem ptr info */
	{
		d2h_w_idx_ptr = ltoh32(ring_info->d2h_w_idx_ptr);
		d2h_r_idx_ptr = ltoh32(ring_info->d2h_r_idx_ptr);
		h2d_w_idx_ptr = ltoh32(ring_info->h2d_w_idx_ptr);
		h2d_r_idx_ptr = ltoh32(ring_info->h2d_r_idx_ptr);

		/* Store h2d common ring write/read pointers */
		for (i = 0; i < BCMPCIE_H2D_COMMON_MSGRINGS; i++) {
			bus->ring_sh[i].ring_state_w = h2d_w_idx_ptr;
			bus->ring_sh[i].ring_state_r = h2d_r_idx_ptr;

			/* update mem block */
			h2d_w_idx_ptr = h2d_w_idx_ptr + bus->rw_index_sz;
			h2d_r_idx_ptr = h2d_r_idx_ptr + bus->rw_index_sz;

			DHD_INFO(("h2d w/r : idx %d write %x read %x \n", i,
				bus->ring_sh[i].ring_state_w, bus->ring_sh[i].ring_state_r));
		}

		/* Store d2h common ring write/read pointers */
		for (j = 0; j < BCMPCIE_D2H_COMMON_MSGRINGS; j++, i++) {
			bus->ring_sh[i].ring_state_w = d2h_w_idx_ptr;
			bus->ring_sh[i].ring_state_r = d2h_r_idx_ptr;

			/* update mem block */
			d2h_w_idx_ptr = d2h_w_idx_ptr + bus->rw_index_sz;
			d2h_r_idx_ptr = d2h_r_idx_ptr + bus->rw_index_sz;

			DHD_INFO(("d2h w/r : idx %d write %x read %x \n", i,
				bus->ring_sh[i].ring_state_w, bus->ring_sh[i].ring_state_r));
		}

		/* Store txflow ring write/read pointers */
		if (bus->api.fw_rev < PCIE_SHARED_VERSION_6) {
			max_tx_flowrings -= BCMPCIE_H2D_COMMON_MSGRINGS;
		} else {
			/* Account for Debug info h2d ring located after the last tx flow ring */
			max_tx_flowrings = max_tx_flowrings + 1;
		}
		for (j = 0; j < max_tx_flowrings; i++, j++)
		{
			bus->ring_sh[i].ring_state_w = h2d_w_idx_ptr;
			bus->ring_sh[i].ring_state_r = h2d_r_idx_ptr;

			/* update mem block */
			h2d_w_idx_ptr = h2d_w_idx_ptr + bus->rw_index_sz;
			h2d_r_idx_ptr = h2d_r_idx_ptr + bus->rw_index_sz;

			DHD_INFO(("FLOW Rings h2d w/r : idx %d write %x read %x \n", i,
				bus->ring_sh[i].ring_state_w,
				bus->ring_sh[i].ring_state_r));
		}
		/* store wr/rd pointers for  debug info completion ring */
		bus->ring_sh[i].ring_state_w = d2h_w_idx_ptr;
		bus->ring_sh[i].ring_state_r = d2h_r_idx_ptr;
		d2h_w_idx_ptr = d2h_w_idx_ptr + bus->rw_index_sz;
		d2h_r_idx_ptr = d2h_r_idx_ptr + bus->rw_index_sz;
		DHD_INFO(("d2h w/r : idx %d write %x read %x \n", i,
			bus->ring_sh[i].ring_state_w, bus->ring_sh[i].ring_state_r));
	}
} /* dhd_fillup_ring_sharedptr_info */

/**
 * Initialize bus module: prepare for communication with the dongle. Called after downloading
 * firmware into the dongle.
 */
int dhd_bus_init(dhd_pub_t *dhdp, bool enforce_mutex)
{
	dhd_bus_t *bus = dhdp->bus;
	int  ret = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(bus->dhd);
	if (!bus->dhd)
		return 0;

	if (PCIE_RELOAD_WAR_ENAB(bus->sih->buscorerev)) {
		dhd_bus_pcie_pwr_req_clear_reload_war(bus);
	}

	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req(bus);
	}

	/* Configure AER registers to log the TLP header */
	dhd_bus_aer_config(bus);

	/* Make sure we're talking to the core. */
	bus->reg = si_setcore(bus->sih, PCIE2_CORE_ID, 0);
	ASSERT(bus->reg != NULL);

	/* before opening up bus for data transfer, check if shared are is intact */

	/* Do minimum console buffer read */
	/* This helps in getting trap messages if any */
	if ((ret = dhdpcie_readshared_console(bus)) >= 0) {
		if ((ret = dhdpcie_bus_readconsole(bus)) < 0) {
			DHD_ERROR(("%s: Console buffer read failed\n",
					__FUNCTION__));
		}
	}

	ret = dhdpcie_readshared(bus);
	if (ret < 0) {
		DHD_ERROR(("%s :Shared area read failed \n", __FUNCTION__));
		goto exit;
	}

	/* Make sure we're talking to the core. */
	bus->reg = si_setcore(bus->sih, PCIE2_CORE_ID, 0);
	ASSERT(bus->reg != NULL);

	dhd_init_bus_lock(bus);

	dhd_init_backplane_access_lock(bus);

	/* Set bus state according to enable result */
	dhdp->busstate = DHD_BUS_DATA;
	bus->bus_low_power_state = DHD_BUS_NO_LOW_POWER_STATE;
	dhdp->dhd_bus_busy_state = 0;

	/* D11 status via PCIe completion header */
	if ((ret = dhdpcie_init_d11status(bus)) < 0) {
		goto exit;
	}

	if (!dhd_download_fw_on_driverload)
		dhd_dpc_enable(bus->dhd);
	/* Enable the interrupt after device is up */
	dhdpcie_bus_intr_enable(bus);

	bus->intr_enabled = TRUE;

	/* bcmsdh_intr_unmask(bus->sdh); */
#ifdef DHD_PCIE_RUNTIMEPM
	bus->idlecount = 0;
	bus->idletime = (int32)MAX_IDLE_COUNT;
	init_waitqueue_head(&bus->rpm_queue);
	mutex_init(&bus->pm_lock);
#else
	bus->idletime = 0;
#endif /* DHD_PCIE_RUNTIMEPM */

	/* Make use_d0_inform TRUE for Rev 5 for backward compatibility */
	if (bus->api.fw_rev < PCIE_SHARED_VERSION_6) {
		bus->use_d0_inform = TRUE;
	} else {
		bus->use_d0_inform = FALSE;
	}

exit:
	if (MULTIBP_ENAB(bus->sih)) {
		dhd_bus_pcie_pwr_req_clear(bus);
	}
	return ret;
}

static void
dhdpcie_init_shared_addr(dhd_bus_t *bus)
{
	uint32 addr = 0;
	uint32 val = 0;

	addr = bus->dongle_ram_base + bus->ramsize - 4;
#ifdef DHD_PCIE_RUNTIMEPM
	dhdpcie_runtime_bus_wake(bus->dhd, TRUE, __builtin_return_address(0));
#endif /* DHD_PCIE_RUNTIMEPM */
	dhdpcie_bus_membytes(bus, TRUE, addr, (uint8 *)&val, sizeof(val));
}

bool
dhdpcie_chipmatch(uint16 vendor, uint16 device)
{

	if (vendor == PCI_VENDOR_ID_BROADCOM || vendor == PCI_VENDOR_ID_CYPRESS) {
		DHD_ERROR(("%s: Supporting vendor %x device %x\n", __FUNCTION__,
			vendor, device));
	} else {
		DHD_ERROR(("%s: Unsupported vendor %x device %x\n", __FUNCTION__,
			vendor, device));
		return (-ENODEV);
	}

	if ((device == BCM4350_D11AC_ID) || (device == BCM4350_D11AC2G_ID) ||
		(device == BCM4350_D11AC5G_ID) || (device == BCM4350_CHIP_ID) ||
		(device == BCM43569_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM4354_D11AC_ID) || (device == BCM4354_D11AC2G_ID) ||
		(device == BCM4354_D11AC5G_ID) || (device == BCM4354_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM4356_D11AC_ID) || (device == BCM4356_D11AC2G_ID) ||
		(device == BCM4356_D11AC5G_ID) || (device == BCM4356_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM4371_D11AC_ID) || (device == BCM4371_D11AC2G_ID) ||
		(device == BCM4371_D11AC5G_ID) || (device == BCM4371_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM4345_D11AC_ID) || (device == BCM4345_D11AC2G_ID) ||
		(device == BCM4345_D11AC5G_ID) || BCM4345_CHIP(device)) {
		return 0;
	}

	if ((device == BCM43452_D11AC_ID) || (device == BCM43452_D11AC2G_ID) ||
		(device == BCM43452_D11AC5G_ID)) {
		return 0;
	}

	if ((device == BCM4335_D11AC_ID) || (device == BCM4335_D11AC2G_ID) ||
		(device == BCM4335_D11AC5G_ID) || (device == BCM4335_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM43602_D11AC_ID) || (device == BCM43602_D11AC2G_ID) ||
		(device == BCM43602_D11AC5G_ID) || (device == BCM43602_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM43569_D11AC_ID) || (device == BCM43569_D11AC2G_ID) ||
		(device == BCM43569_D11AC5G_ID) || (device == BCM43569_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM4358_D11AC_ID) || (device == BCM4358_D11AC2G_ID) ||
		(device == BCM4358_D11AC5G_ID)) {
		return 0;
	}

	if ((device == BCM4349_D11AC_ID) || (device == BCM4349_D11AC2G_ID) ||
		(device == BCM4349_D11AC5G_ID) || (device == BCM4349_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM4355_D11AC_ID) || (device == BCM4355_D11AC2G_ID) ||
		(device == BCM4355_D11AC5G_ID) || (device == BCM4355_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM4359_D11AC_ID) || (device == BCM4359_D11AC2G_ID) ||
		(device == BCM4359_D11AC5G_ID)) {
		return 0;
	}

	if ((device == BCM43596_D11AC_ID) || (device == BCM43596_D11AC2G_ID) ||
		(device == BCM43596_D11AC5G_ID)) {
		return 0;
	}

	if ((device == BCM43597_D11AC_ID) || (device == BCM43597_D11AC2G_ID) ||
		(device == BCM43597_D11AC5G_ID)) {
		return 0;
	}

	if ((device == BCM4364_D11AC_ID) || (device == BCM4364_D11AC2G_ID) ||
		(device == BCM4364_D11AC5G_ID) || (device == BCM4364_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM4361_D11AC_ID) || (device == BCM4361_D11AC2G_ID) ||
		(device == BCM4361_D11AC5G_ID) || (device == BCM4361_CHIP_ID)) {
		return 0;
	}
	if ((device == BCM4362_D11AX_ID) || (device == BCM4362_D11AX2G_ID) ||
		(device == BCM4362_D11AX5G_ID) || (device == BCM4362_CHIP_ID)) {
		return 0;
	}
	if ((device == BCM43751_D11AX_ID) || (device == BCM43751_D11AX2G_ID) ||
		(device == BCM43751_D11AX5G_ID) || (device == BCM43751_CHIP_ID)) {
		return 0;
	}
	if ((device == BCM4347_D11AC_ID) || (device == BCM4347_D11AC2G_ID) ||
		(device == BCM4347_D11AC5G_ID) || (device == BCM4347_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM4365_D11AC_ID) || (device == BCM4365_D11AC2G_ID) ||
		(device == BCM4365_D11AC5G_ID) || (device == BCM4365_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM4366_D11AC_ID) || (device == BCM4366_D11AC2G_ID) ||
		(device == BCM4366_D11AC5G_ID) || (device == BCM4366_CHIP_ID) ||
		(device == BCM43664_CHIP_ID) || (device == BCM43666_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM4369_D11AX_ID) || (device == BCM4369_D11AX2G_ID) ||
		(device == BCM4369_D11AX5G_ID) || (device == BCM4369_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM4373_D11AC_ID) || (device == BCM4373_D11AC2G_ID) ||
		(device == BCM4373_D11AC5G_ID) || (device == BCM4373_CHIP_ID)) {
		return 0;
	}

	if ((device == BCM4375_D11AX_ID) || (device == BCM4375_D11AX2G_ID) ||
		(device == BCM4375_D11AX5G_ID) || (device == BCM4375_CHIP_ID)) {
		return 0;
	}

#ifdef CHIPS_CUSTOMER_HW6
	if ((device == BCM4376_D11AC_ID) || (device == BCM4376_D11AC2G_ID) ||
		(device == BCM4376_D11AC5G_ID) || (device == BCM4376_CHIP_ID)) {
		return 0;
	}
	if ((device == BCM4377_M_D11AX_ID) || (device == BCM4377_D11AX_ID) ||
		(device == BCM4377_D11AX2G_ID) || (device == BCM4377_D11AX5G_ID) ||
		(device == BCM4377_CHIP_ID)) {
		return 0;
	}
	if ((device == BCM4378_D11AC_ID) || (device == BCM4378_D11AC2G_ID) ||
		(device == BCM4378_D11AC5G_ID) || (device == BCM4378_CHIP_ID)) {
		return 0;
	}
#endif /* CHIPS_CUSTOMER_HW6 */
#ifdef CHIPS_CUSTOMER_HW6
	if ((device == BCM4368_D11AC_ID) || (device == BCM4368_D11AC2G_ID) ||
		(device == BCM4368_D11AC5G_ID) || (device == BCM4368_CHIP_ID)) {
		return 0;
	}
	if ((device == BCM4367_D11AC_ID) || (device == BCM4367_D11AC2G_ID) ||
		(device == BCM4367_D11AC5G_ID) || (device == BCM4367_CHIP_ID)) {
		return 0;
	}
#endif /* CHIPS_CUSTOMER_HW6 */

	/* CYW55560 */
	if (device == CYW55560_WLAN_ID) {
		return 0;
	}
	DHD_ERROR(("%s: Unsupported vendor %x device %x\n", __FUNCTION__, vendor, device));
	return (-ENODEV);
} /* dhdpcie_chipmatch */

/*
 * Name:  dhdpcie_sromotp_customvar
 * Description:
 * read otp/sprom and parse & store customvar.
 * A shadow of OTP/SPROM exists in ChipCommon Region
 * betw. 0x800 and 0xBFF (Backplane Addr. 0x1800_0800 and 0x1800_0BFF).
 * Strapping option (SPROM vs. OTP), presence of OTP/SPROM and its size
 * can also be read from ChipCommon Registers.
 */
static int
dhdpcie_sromotp_customvar(dhd_bus_t *bus,  uint32 *customvar1, uint32 *customvar2)
{
	uint16 dump_offset = 0;
	uint32 dump_size = 0, otp_size = 0, sprom_size = 0;
	/* Table for 65nm OTP Size (in bits) */
	int  otp_size_65nm[8] = {0, 2048, 4096, 8192, 4096, 6144, 512, 1024};
	volatile uint16 *nvm_shadow;
	uint cur_coreid;
	uint chipc_corerev;
	chipcregs_t *chipcregs;
	uint16 *otp_dump;
	uint8 *cis;
	uint8 tup, tlen;
	int i = 0;

	/* Save the current core */
	cur_coreid = si_coreid(bus->sih);
	/* Switch to ChipC */
	chipcregs = (chipcregs_t *)si_setcore(bus->sih, CC_CORE_ID, 0);
	ASSERT(chipcregs != NULL);
	chipc_corerev = si_corerev(bus->sih);
	/* Check ChipcommonCore Rev */
	if (chipc_corerev < 44) {
		DHD_ERROR(("%s: ChipcommonCore Rev %d < 44\n", __FUNCTION__, chipc_corerev));
		return BCME_UNSUPPORTED;
	}
	/* Check ChipID */
	if (((uint16)bus->sih->chip != BCM4350_CHIP_ID) && !BCM4345_CHIP((uint16)bus->sih->chip) &&
		((uint16)bus->sih->chip != BCM4355_CHIP_ID) &&
		((uint16)bus->sih->chip != BCM4359_CHIP_ID) &&
		((uint16)bus->sih->chip != BCM4349_CHIP_ID)) {
		DHD_ERROR(("%s: supported for chips"
				"4350/4345/4355/4364/4349/4359 only\n", __FUNCTION__));
		return BCME_UNSUPPORTED;
	}
	/* Check if SRC_PRESENT in SpromCtrl(0x190 in ChipCommon Regs) is set */
	if (chipcregs->sromcontrol & SRC_PRESENT) {
		/* SPROM Size: 1Kbits (0x0), 4Kbits (0x1), 16Kbits(0x2) */
		sprom_size = (1 << (2 * ((chipcregs->sromcontrol & SRC_SIZE_MASK)
				>> SRC_SIZE_SHIFT))) * 1024;
		DHD_TRACE(("\nSPROM Present (Size %d bits)\n", sprom_size));
	}
	if (chipcregs->sromcontrol & SRC_OTPPRESENT) {
		DHD_TRACE(("\nOTP Present"));
		if (((chipcregs->otplayout & OTPL_WRAP_TYPE_MASK) >> OTPL_WRAP_TYPE_SHIFT)
				== OTPL_WRAP_TYPE_40NM) {
			/* 40nm OTP: Size = (OtpSize + 1) * 1024 bits */
			/* Chipcommon rev51 is a variation on rev45 and does not support
			* the latest OTP configuration.
			*/
			if (chipc_corerev != 51 && chipc_corerev >= 49) {
				otp_size = (((chipcregs->otplayout & OTPL_ROW_SIZE_MASK)
					>> OTPL_ROW_SIZE_SHIFT) + 1) * 1024;
				DHD_TRACE(("(Size %d bits)\n", otp_size));
			} else {
				otp_size =  (((chipcregs->capabilities & CC_CAP_OTPSIZE)
						>> CC_CAP_OTPSIZE_SHIFT) + 1) * 1024;
				DHD_TRACE(("(Size %d bits)\n", otp_size));
			}
		} else {
			/* This part is untested since newer chips have 40nm OTP */
			/* Chipcommon rev51 is a variation on rev45 and does not support
			* the latest OTP configuration.
			*/
				if (chipc_corerev != 51 && chipc_corerev >= 49) {
					otp_size = otp_size_65nm[(chipcregs->otplayout &
							OTPL_ROW_SIZE_MASK) >> OTPL_ROW_SIZE_SHIFT];
					DHD_TRACE(("(Size %d bits)\n", otp_size));
				} else {
					otp_size = otp_size_65nm[(chipcregs->capabilities &
							CC_CAP_OTPSIZE)	>> CC_CAP_OTPSIZE_SHIFT];
					DHD_TRACE(("(Size %d bits)\n", otp_size));
					DHD_TRACE(("%s: 65nm/130nm OTP Size not tested. \n",
							__FUNCTION__));
				}
		}
	}
	/* Chipcommon rev51 is a variation on rev45 and does not support
	* the latest OTP configuration.
	*/
	if (chipc_corerev != 51 && chipc_corerev >= 49) {
		if (((chipcregs->sromcontrol & SRC_PRESENT) == 0) &&
				((chipcregs->otplayout & OTPL_ROW_SIZE_MASK) == 0)) {
			DHD_ERROR(("%s: SPROM and OTP could not be found "
					"sromcontrol = %x, otplayout = %x \n",
					__FUNCTION__, chipcregs->sromcontrol,
					chipcregs->otplayout));
			return BCME_NOTFOUND;
		}
	} else {
		if (((chipcregs->sromcontrol & SRC_PRESENT) == 0) &&
			((chipcregs->capabilities & CC_CAP_OTPSIZE) == 0)) {
			DHD_ERROR(("%s: SPROM and OTP could not be found "
					"sromcontrol = %x, capablities = %x \n",
					__FUNCTION__, chipcregs->sromcontrol,
					chipcregs->capabilities));
			return BCME_NOTFOUND;
		}
	}
	/* Check the strapping option in SpromCtrl: Set = OTP otherwise SPROM */
	if ((!(chipcregs->sromcontrol & SRC_PRESENT) || (chipcregs->sromcontrol & SRC_OTPSEL)) &&
			(chipcregs->sromcontrol & SRC_OTPPRESENT)) {
		DHD_TRACE(("OTP Strap selected.\n"
				"\nOTP Shadow in ChipCommon:\n"));
		dump_size = otp_size / 16 ; /* 16bit words */
	} else if (((chipcregs->sromcontrol & SRC_OTPSEL) == 0) &&
			(chipcregs->sromcontrol & SRC_PRESENT)) {
		DHD_TRACE(("SPROM Strap selected\n"
				"\nSPROM Shadow in ChipCommon:\n"));
		/* If SPROM > 8K only 8Kbits is mapped to ChipCommon (0x800 - 0xBFF) */
		/* dump_size in 16bit words */
		dump_size = sprom_size > 8 ? (8 * 1024) / 16 : sprom_size / 16;
	} else {
		DHD_ERROR(("%s: NVM Shadow does not exist in ChipCommon\n",
				__FUNCTION__));
		return BCME_NOTFOUND;
	}
	if (bus->regs == NULL) {
		DHD_ERROR(("ChipCommon Regs. not initialized\n"));
		return BCME_NOTREADY;
	} else {
		/* Chipcommon rev51 is a variation on rev45 and does not support
		* the latest OTP configuration.
		*/
		if (chipc_corerev != 51 && chipc_corerev >= 49) {
			/* Chip common can read only 8kbits,
			* for ccrev >= 49 otp size is around 12 kbits so use GCI core
			*/
			nvm_shadow = (volatile uint16 *)si_setcore(bus->sih, GCI_CORE_ID, 0);
		}  else {
			/* Point to the SPROM/OTP shadow in ChipCommon */
			nvm_shadow = chipcregs->sromotp;
		}
		if (nvm_shadow == NULL) {
			DHD_ERROR(("%s: NVM Shadow is not intialized\n", __FUNCTION__));
			return BCME_NOTFOUND;
		}
		otp_dump = kzalloc(dump_size*2, GFP_KERNEL);
		if (otp_dump == NULL) {
			DHD_ERROR(("%s: Insufficient system memory of size %d\n",
				__FUNCTION__, dump_size));
			return BCME_NOMEM;
		}
		/*
		* Read 16 bits / iteration.
		* dump_size & dump_offset in 16-bit words
		*/
		while (dump_offset < dump_size) {
			*(otp_dump + dump_offset) = *(nvm_shadow + dump_offset);
			dump_offset += 0x1;
		}
		/* Read from cis tuple start address */
		cis = (uint8 *)otp_dump + CISTPL_OFFSET;
		/* parse value of customvar2 tuple */
		do {
			tup = cis[i++];
			if (tup == CISTPL_NULL || tup == CISTPL_END)
				tlen = 0;
			else
				tlen = cis[i++];
			if ((i + tlen) >= dump_size*2)
				break;
			switch (tup) {
				case CISTPL_BRCM_HNBU:
				switch (cis[i]) {
					case HNBU_CUSTOM1:
						*customvar1 = ((cis[i + 4] << 24) +
								(cis[i + 3] << 16) +
								(cis[i + 2] << 8) +
								cis[i + 1]);
						DHD_TRACE(("%s : customvar1 [%x]\n",
								__FUNCTION__, *customvar1));
						break;
					case HNBU_CUSTOM2:
						*customvar2 = ((cis[i + 4] << 24) +
								(cis[i + 3] << 16) +
								(cis[i + 2] << 8) +
								cis[i + 1]);
						DHD_TRACE(("%s : customvar2 [%x]\n",
							__FUNCTION__, *customvar2));
						break;
					default:
						break;
				}
					break;
				default:
					break;
			}
			i += tlen;
		} while (tup != 0xff);

		if (otp_dump) {
			kfree(otp_dump);
			otp_dump = NULL;
		}
	}
	/* Switch back to the original core */
	si_setcore(bus->sih, cur_coreid, 0);
	return BCME_OK;
} /* dhdpcie_sromotp_customvar */

/**
 * Name:  dhdpcie_cc_nvmshadow
 *
 * Description:
 * A shadow of OTP/SPROM exists in ChipCommon Region
 * betw. 0x800 and 0xBFF (Backplane Addr. 0x1800_0800 and 0x1800_0BFF).
 * Strapping option (SPROM vs. OTP), presence of OTP/SPROM and its size
 * can also be read from ChipCommon Registers.
 */
static int
dhdpcie_cc_nvmshadow(dhd_bus_t *bus, struct bcmstrbuf *b)
{
	uint16 dump_offset = 0;
	uint32 dump_size = 0, otp_size = 0, sprom_size = 0;

	/* Table for 65nm OTP Size (in bits) */
	int  otp_size_65nm[8] = {0, 2048, 4096, 8192, 4096, 6144, 512, 1024};

	volatile uint16 *nvm_shadow;

	uint cur_coreid;
	uint chipc_corerev;
	chipcregs_t *chipcregs;

	/* Save the current core */
	cur_coreid = si_coreid(bus->sih);
	/* Switch to ChipC */
	chipcregs = (chipcregs_t *)si_setcore(bus->sih, CC_CORE_ID, 0);
	ASSERT(chipcregs != NULL);

	chipc_corerev = si_corerev(bus->sih);

	/* Check ChipcommonCore Rev */
	if (chipc_corerev < 44) {
		DHD_ERROR(("%s: ChipcommonCore Rev %d < 44\n", __FUNCTION__, chipc_corerev));
		return BCME_UNSUPPORTED;
	}

	/* Check ChipID */
	if (((uint16)bus->sih->chip != BCM4350_CHIP_ID) && !BCM4345_CHIP((uint16)bus->sih->chip) &&
	        ((uint16)bus->sih->chip != BCM4355_CHIP_ID) &&
	        ((uint16)bus->sih->chip != BCM4364_CHIP_ID)) {
		DHD_ERROR(("%s: cc_nvmdump cmd. supported for Olympic chips"
					"4350/4345/4355/4364 only\n", __FUNCTION__));
		return BCME_UNSUPPORTED;
	}

	/* Check if SRC_PRESENT in SpromCtrl(0x190 in ChipCommon Regs) is set */
	if (chipcregs->sromcontrol & SRC_PRESENT) {
		/* SPROM Size: 1Kbits (0x0), 4Kbits (0x1), 16Kbits(0x2) */
		sprom_size = (1 << (2 * ((chipcregs->sromcontrol & SRC_SIZE_MASK)
					>> SRC_SIZE_SHIFT))) * 1024;
		bcm_bprintf(b, "\nSPROM Present (Size %d bits)\n", sprom_size);
	}

	if (chipcregs->sromcontrol & SRC_OTPPRESENT) {
		bcm_bprintf(b, "\nOTP Present");

		if (((chipcregs->otplayout & OTPL_WRAP_TYPE_MASK) >> OTPL_WRAP_TYPE_SHIFT)
			== OTPL_WRAP_TYPE_40NM) {
			/* 40nm OTP: Size = (OtpSize + 1) * 1024 bits */
			/* Chipcommon rev51 is a variation on rev45 and does not support
			 * the latest OTP configuration.
			 */
			if (chipc_corerev != 51 && chipc_corerev >= 49) {
				otp_size = (((chipcregs->otplayout & OTPL_ROW_SIZE_MASK)
					>> OTPL_ROW_SIZE_SHIFT) + 1) * 1024;
				bcm_bprintf(b, "(Size %d bits)\n", otp_size);
			} else {
				otp_size =  (((chipcregs->capabilities & CC_CAP_OTPSIZE)
				        >> CC_CAP_OTPSIZE_SHIFT) + 1) * 1024;
				bcm_bprintf(b, "(Size %d bits)\n", otp_size);
			}
		} else {
			/* This part is untested since newer chips have 40nm OTP */
			/* Chipcommon rev51 is a variation on rev45 and does not support
			 * the latest OTP configuration.
			 */
			if (chipc_corerev != 51 && chipc_corerev >= 49) {
				otp_size = otp_size_65nm[(chipcregs->otplayout & OTPL_ROW_SIZE_MASK)
						>> OTPL_ROW_SIZE_SHIFT];
				bcm_bprintf(b, "(Size %d bits)\n", otp_size);
			} else {
				otp_size = otp_size_65nm[(chipcregs->capabilities & CC_CAP_OTPSIZE)
					        >> CC_CAP_OTPSIZE_SHIFT];
				bcm_bprintf(b, "(Size %d bits)\n", otp_size);
				DHD_INFO(("%s: 65nm/130nm OTP Size not tested. \n",
					__FUNCTION__));
			}
		}
	}

	/* Chipcommon rev51 is a variation on rev45 and does not support
	 * the latest OTP configuration.
	 */
	if (chipc_corerev != 51 && chipc_corerev >= 49) {
		if (((chipcregs->sromcontrol & SRC_PRESENT) == 0) &&
			((chipcregs->otplayout & OTPL_ROW_SIZE_MASK) == 0)) {
			DHD_ERROR(("%s: SPROM and OTP could not be found "
				"sromcontrol = %x, otplayout = %x \n",
				__FUNCTION__, chipcregs->sromcontrol, chipcregs->otplayout));
			return BCME_NOTFOUND;
		}
	} else {
		if (((chipcregs->sromcontrol & SRC_PRESENT) == 0) &&
			((chipcregs->capabilities & CC_CAP_OTPSIZE) == 0)) {
			DHD_ERROR(("%s: SPROM and OTP could not be found "
				"sromcontrol = %x, capablities = %x \n",
				__FUNCTION__, chipcregs->sromcontrol, chipcregs->capabilities));
			return BCME_NOTFOUND;
		}
	}

	/* Check the strapping option in SpromCtrl: Set = OTP otherwise SPROM */
	if ((!(chipcregs->sromcontrol & SRC_PRESENT) || (chipcregs->sromcontrol & SRC_OTPSEL)) &&
		(chipcregs->sromcontrol & SRC_OTPPRESENT)) {

		bcm_bprintf(b, "OTP Strap selected.\n"
		               "\nOTP Shadow in ChipCommon:\n");

		dump_size = otp_size / 16 ; /* 16bit words */

	} else if (((chipcregs->sromcontrol & SRC_OTPSEL) == 0) &&
		(chipcregs->sromcontrol & SRC_PRESENT)) {

		bcm_bprintf(b, "SPROM Strap selected\n"
				"\nSPROM Shadow in ChipCommon:\n");

		/* If SPROM > 8K only 8Kbits is mapped to ChipCommon (0x800 - 0xBFF) */
		/* dump_size in 16bit words */
		dump_size = sprom_size > 8 ? (8 * 1024) / 16 : sprom_size / 16;
	} else {
		DHD_ERROR(("%s: NVM Shadow does not exist in ChipCommon\n",
			__FUNCTION__));
		return BCME_NOTFOUND;
	}

	if (bus->regs == NULL) {
		DHD_ERROR(("ChipCommon Regs. not initialized\n"));
		return BCME_NOTREADY;
	} else {
		bcm_bprintf(b, "\n OffSet:");

		/* Chipcommon rev51 is a variation on rev45 and does not support
		 * the latest OTP configuration.
		 */
		if (chipc_corerev != 51 && chipc_corerev >= 49) {
			/* Chip common can read only 8kbits,
			* for ccrev >= 49 otp size is around 12 kbits so use GCI core
			*/
			nvm_shadow = (volatile uint16 *)si_setcore(bus->sih, GCI_CORE_ID, 0);
		} else {
			/* Point to the SPROM/OTP shadow in ChipCommon */
			nvm_shadow = chipcregs->sromotp;
		}

		if (nvm_shadow == NULL) {
			DHD_ERROR(("%s: NVM Shadow is not intialized\n", __FUNCTION__));
			return BCME_NOTFOUND;
		}

		/*
		* Read 16 bits / iteration.
		* dump_size & dump_offset in 16-bit words
		*/
		while (dump_offset < dump_size) {
			if (dump_offset % 2 == 0)
				/* Print the offset in the shadow space in Bytes */
				bcm_bprintf(b, "\n 0x%04x", dump_offset * 2);

			bcm_bprintf(b, "\t0x%04x", *(nvm_shadow + dump_offset));
			dump_offset += 0x1;
		}
	}

	/* Switch back to the original core */
	si_setcore(bus->sih, cur_coreid, 0);

	return BCME_OK;
} /* dhdpcie_cc_nvmshadow */

/** Flow rings are dynamically created and destroyed */
void dhd_bus_clean_flow_ring(dhd_bus_t *bus, void *node)
{
	void *pkt;
	flow_queue_t *queue;
	flow_ring_node_t *flow_ring_node = (flow_ring_node_t *)node;
	unsigned long flags;

	queue = &flow_ring_node->queue;

#ifdef DHDTCPACK_SUPPRESS
	/* Clean tcp_ack_info_tbl in order to prevent access to flushed pkt,
	 * when there is a newly coming packet from network stack.
	 */
	dhd_tcpack_info_tbl_clean(bus->dhd);
#endif /* DHDTCPACK_SUPPRESS */

#ifdef DHD_HP2P
	if (flow_ring_node->hp2p_ring) {
		bus->dhd->hp2p_ring_active = FALSE;
		flow_ring_node->hp2p_ring = FALSE;
	}
#endif /* DHD_HP2P */

	/* clean up BUS level info */
	DHD_FLOWRING_LOCK(flow_ring_node->lock, flags);

	/* Flush all pending packets in the queue, if any */
	while ((pkt = dhd_flow_queue_dequeue(bus->dhd, queue)) != NULL) {
		PKTFREE(bus->dhd->osh, pkt, TRUE);
	}
	ASSERT(DHD_FLOW_QUEUE_EMPTY(queue));

	/* Reinitialise flowring's queue */
	dhd_flow_queue_reinit(bus->dhd, queue, FLOW_RING_QUEUE_THRESHOLD);
	flow_ring_node->status = FLOW_RING_STATUS_CLOSED;
	flow_ring_node->active = FALSE;

	DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);

	/* Hold flowring_list_lock to ensure no race condition while accessing the List */
	DHD_FLOWRING_LIST_LOCK(bus->dhd->flowring_list_lock, flags);
	dll_delete(&flow_ring_node->list);
	DHD_FLOWRING_LIST_UNLOCK(bus->dhd->flowring_list_lock, flags);

	/* Release the flowring object back into the pool */
	dhd_prot_flowrings_pool_release(bus->dhd,
		flow_ring_node->flowid, flow_ring_node->prot_info);

	/* Free the flowid back to the flowid allocator */
	dhd_flowid_free(bus->dhd, flow_ring_node->flow_info.ifindex,
	                flow_ring_node->flowid);
}

/**
 * Allocate a Flow ring buffer,
 * Init Ring buffer, send Msg to device about flow ring creation
*/
int
dhd_bus_flow_ring_create_request(dhd_bus_t *bus, void *arg)
{
	flow_ring_node_t *flow_ring_node = (flow_ring_node_t *)arg;

	DHD_INFO(("%s :Flow create\n", __FUNCTION__));

	/* Send Msg to device about flow ring creation */
	if (dhd_prot_flow_ring_create(bus->dhd, flow_ring_node) != BCME_OK)
		return BCME_NOMEM;

	return BCME_OK;
}

/** Handle response from dongle on a 'flow ring create' request */
void
dhd_bus_flow_ring_create_response(dhd_bus_t *bus, uint16 flowid, int32 status)
{
	flow_ring_node_t *flow_ring_node;
	unsigned long flags;

	DHD_INFO(("%s :Flow Response %d \n", __FUNCTION__, flowid));

	/* Boundary check of the flowid */
	if (flowid >= bus->dhd->num_flow_rings) {
		DHD_ERROR(("%s: flowid is invalid %d, max %d\n", __FUNCTION__,
			flowid, bus->dhd->num_flow_rings));
		return;
	}

	flow_ring_node = DHD_FLOW_RING(bus->dhd, flowid);
	if (!flow_ring_node) {
		DHD_ERROR(("%s: flow_ring_node is NULL\n", __FUNCTION__));
		return;
	}

	ASSERT(flow_ring_node->flowid == flowid);
	if (flow_ring_node->flowid != flowid) {
		DHD_ERROR(("%s: flowid %d is different from the flowid "
			"of the flow_ring_node %d\n", __FUNCTION__, flowid,
			flow_ring_node->flowid));
		return;
	}

	if (status != BCME_OK) {
		DHD_ERROR(("%s Flow create Response failure error status = %d \n",
		     __FUNCTION__, status));
		/* Call Flow clean up */
		dhd_bus_clean_flow_ring(bus, flow_ring_node);
		return;
	}

	DHD_FLOWRING_LOCK(flow_ring_node->lock, flags);
	flow_ring_node->status = FLOW_RING_STATUS_OPEN;
	DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);

	/* Now add the Flow ring node into the active list
	 * Note that this code to add the newly created node to the active
	 * list was living in dhd_flowid_lookup. But note that after
	 * adding the node to the active list the contents of node is being
	 * filled in dhd_prot_flow_ring_create.
	 * If there is a D2H interrupt after the node gets added to the
	 * active list and before the node gets populated with values
	 * from the Bottom half dhd_update_txflowrings would be called.
	 * which will then try to walk through the active flow ring list,
	 * pickup the nodes and operate on them. Now note that since
	 * the function dhd_prot_flow_ring_create is not finished yet
	 * the contents of flow_ring_node can still be NULL leading to
	 * crashes. Hence the flow_ring_node should be added to the
	 * active list only after its truely created, which is after
	 * receiving the create response message from the Host.
	 */
	DHD_FLOWRING_LIST_LOCK(bus->dhd->flowring_list_lock, flags);
	dll_prepend(&bus->flowring_active_list, &flow_ring_node->list);
	DHD_FLOWRING_LIST_UNLOCK(bus->dhd->flowring_list_lock, flags);

	dhd_bus_schedule_queue(bus, flowid, FALSE); /* from queue to flowring */

	return;
}

int
dhd_bus_flow_ring_delete_request(dhd_bus_t *bus, void *arg)
{
	void * pkt;
	flow_queue_t *queue;
	flow_ring_node_t *flow_ring_node;
	unsigned long flags;

	DHD_INFO(("%s :Flow Delete\n", __FUNCTION__));

	flow_ring_node = (flow_ring_node_t *)arg;

#ifdef DHDTCPACK_SUPPRESS
	/* Clean tcp_ack_info_tbl in order to prevent access to flushed pkt,
	 * when there is a newly coming packet from network stack.
	 */
	dhd_tcpack_info_tbl_clean(bus->dhd);
#endif /* DHDTCPACK_SUPPRESS */
	DHD_FLOWRING_LOCK(flow_ring_node->lock, flags);
	if (flow_ring_node->status == FLOW_RING_STATUS_DELETE_PENDING) {
		DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);
		DHD_ERROR(("%s :Delete Pending flowid %u\n", __FUNCTION__, flow_ring_node->flowid));
		return BCME_ERROR;
	}
	flow_ring_node->status = FLOW_RING_STATUS_DELETE_PENDING;

	queue = &flow_ring_node->queue; /* queue associated with flow ring */

	/* Flush all pending packets in the queue, if any */
	while ((pkt = dhd_flow_queue_dequeue(bus->dhd, queue)) != NULL) {
		PKTFREE(bus->dhd->osh, pkt, TRUE);
	}
	ASSERT(DHD_FLOW_QUEUE_EMPTY(queue));

	DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);

	/* Send Msg to device about flow ring deletion */
	dhd_prot_flow_ring_delete(bus->dhd, flow_ring_node);

	return BCME_OK;
}

void
dhd_bus_flow_ring_delete_response(dhd_bus_t *bus, uint16 flowid, uint32 status)
{
	flow_ring_node_t *flow_ring_node;

	DHD_INFO(("%s :Flow Delete Response %d \n", __FUNCTION__, flowid));

	/* Boundary check of the flowid */
	if (flowid >= bus->dhd->num_flow_rings) {
		DHD_ERROR(("%s: flowid is invalid %d, max %d\n", __FUNCTION__,
			flowid, bus->dhd->num_flow_rings));
		return;
	}

	flow_ring_node = DHD_FLOW_RING(bus->dhd, flowid);
	if (!flow_ring_node) {
		DHD_ERROR(("%s: flow_ring_node is NULL\n", __FUNCTION__));
		return;
	}

	ASSERT(flow_ring_node->flowid == flowid);
	if (flow_ring_node->flowid != flowid) {
		DHD_ERROR(("%s: flowid %d is different from the flowid "
			"of the flow_ring_node %d\n", __FUNCTION__, flowid,
			flow_ring_node->flowid));
		return;
	}

	if (status != BCME_OK) {
		DHD_ERROR(("%s Flow Delete Response failure error status = %d \n",
		    __FUNCTION__, status));
		return;
	}
	/* Call Flow clean up */
	dhd_bus_clean_flow_ring(bus, flow_ring_node);

	return;

}

int dhd_bus_flow_ring_flush_request(dhd_bus_t *bus, void *arg)
{
	void *pkt;
	flow_queue_t *queue;
	flow_ring_node_t *flow_ring_node;
	unsigned long flags;

	DHD_INFO(("%s :Flow Flush\n", __FUNCTION__));

	flow_ring_node = (flow_ring_node_t *)arg;

	DHD_FLOWRING_LOCK(flow_ring_node->lock, flags);
	queue = &flow_ring_node->queue; /* queue associated with flow ring */
	/* Flow ring status will be set back to FLOW_RING_STATUS_OPEN
	 * once flow ring flush response is received for this flowring node.
	 */
	flow_ring_node->status = FLOW_RING_STATUS_FLUSH_PENDING;

#ifdef DHDTCPACK_SUPPRESS
	/* Clean tcp_ack_info_tbl in order to prevent access to flushed pkt,
	 * when there is a newly coming packet from network stack.
	 */
	dhd_tcpack_info_tbl_clean(bus->dhd);
#endif /* DHDTCPACK_SUPPRESS */

	/* Flush all pending packets in the queue, if any */
	while ((pkt = dhd_flow_queue_dequeue(bus->dhd, queue)) != NULL) {
		PKTFREE(bus->dhd->osh, pkt, TRUE);
	}
	ASSERT(DHD_FLOW_QUEUE_EMPTY(queue));

	DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);

	/* Send Msg to device about flow ring flush */
	dhd_prot_flow_ring_flush(bus->dhd, flow_ring_node);

	return BCME_OK;
}

void
dhd_bus_flow_ring_flush_response(dhd_bus_t *bus, uint16 flowid, uint32 status)
{
	flow_ring_node_t *flow_ring_node;

	if (status != BCME_OK) {
		DHD_ERROR(("%s Flow flush Response failure error status = %d \n",
		    __FUNCTION__, status));
		return;
	}

	/* Boundary check of the flowid */
	if (flowid >= bus->dhd->num_flow_rings) {
		DHD_ERROR(("%s: flowid is invalid %d, max %d\n", __FUNCTION__,
			flowid, bus->dhd->num_flow_rings));
		return;
	}

	flow_ring_node = DHD_FLOW_RING(bus->dhd, flowid);
	if (!flow_ring_node) {
		DHD_ERROR(("%s: flow_ring_node is NULL\n", __FUNCTION__));
		return;
	}

	ASSERT(flow_ring_node->flowid == flowid);
	if (flow_ring_node->flowid != flowid) {
		DHD_ERROR(("%s: flowid %d is different from the flowid "
			"of the flow_ring_node %d\n", __FUNCTION__, flowid,
			flow_ring_node->flowid));
		return;
	}

	flow_ring_node->status = FLOW_RING_STATUS_OPEN;
	return;
}

uint32
dhd_bus_max_h2d_queues(struct dhd_bus *bus)
{
	return bus->max_submission_rings;
}

/* To be symmetric with SDIO */
void
dhd_bus_pktq_flush(dhd_pub_t *dhdp)
{
	return;
}

void
dhd_bus_set_linkdown(dhd_pub_t *dhdp, bool val)
{
	dhdp->bus->is_linkdown = val;
}

int
dhd_bus_get_linkdown(dhd_pub_t *dhdp)
{
	return dhdp->bus->is_linkdown;
}

int
dhd_bus_get_cto(dhd_pub_t *dhdp)
{
	return dhdp->bus->cto_triggered;
}

#ifdef IDLE_TX_FLOW_MGMT
/* resume request */
int
dhd_bus_flow_ring_resume_request(dhd_bus_t *bus, void *arg)
{
	flow_ring_node_t *flow_ring_node = (flow_ring_node_t *)arg;

	DHD_ERROR(("%s :Flow Resume Request flow id %u\n", __FUNCTION__, flow_ring_node->flowid));

	flow_ring_node->status = FLOW_RING_STATUS_RESUME_PENDING;

	/* Send Msg to device about flow ring resume */
	dhd_prot_flow_ring_resume(bus->dhd, flow_ring_node);

	return BCME_OK;
}

/* add the node back to active flowring */
void
dhd_bus_flow_ring_resume_response(dhd_bus_t *bus, uint16 flowid, int32 status)
{

	flow_ring_node_t *flow_ring_node;

	DHD_TRACE(("%s :flowid %d \n", __FUNCTION__, flowid));

	flow_ring_node = DHD_FLOW_RING(bus->dhd, flowid);
	ASSERT(flow_ring_node->flowid == flowid);

	if (status != BCME_OK) {
		DHD_ERROR(("%s Error Status = %d \n",
			__FUNCTION__, status));
		return;
	}

	DHD_TRACE(("%s :Number of pkts queued in FlowId:%d is -> %u!!\n",
		__FUNCTION__, flow_ring_node->flowid,  flow_ring_node->queue.len));

	flow_ring_node->status = FLOW_RING_STATUS_OPEN;

	dhd_bus_schedule_queue(bus, flowid, FALSE);
	return;
}

/* scan the flow rings in active list for idle time out */
void
dhd_bus_check_idle_scan(dhd_bus_t *bus)
{
	uint64 time_stamp; /* in millisec */
	uint64 diff;

	time_stamp = OSL_SYSUPTIME();
	diff = time_stamp - bus->active_list_last_process_ts;

	if (diff > IDLE_FLOW_LIST_TIMEOUT) {
		dhd_bus_idle_scan(bus);
		bus->active_list_last_process_ts = OSL_SYSUPTIME();
	}

	return;
}

/* scan the nodes in active list till it finds a non idle node */
void
dhd_bus_idle_scan(dhd_bus_t *bus)
{
	dll_t *item, *prev;
	flow_ring_node_t *flow_ring_node;
	uint64 time_stamp, diff;
	unsigned long flags;
	uint16 ringid[MAX_SUSPEND_REQ];
	uint16 count = 0;

	time_stamp = OSL_SYSUPTIME();
	DHD_FLOWRING_LIST_LOCK(bus->dhd->flowring_list_lock, flags);

	for (item = dll_tail_p(&bus->flowring_active_list);
	         !dll_end(&bus->flowring_active_list, item); item = prev) {
		prev = dll_prev_p(item);

		flow_ring_node = dhd_constlist_to_flowring(item);

		if (flow_ring_node->flowid == (bus->max_submission_rings - 1))
			continue;

		if (flow_ring_node->status != FLOW_RING_STATUS_OPEN) {
			/* Takes care of deleting zombie rings */
			/* delete from the active list */
			DHD_INFO(("deleting flow id %u from active list\n",
				flow_ring_node->flowid));
			__dhd_flow_ring_delete_from_active_list(bus, flow_ring_node);
			continue;
		}

		diff = time_stamp - flow_ring_node->last_active_ts;

		if ((diff > IDLE_FLOW_RING_TIMEOUT) && !(flow_ring_node->queue.len))  {
			DHD_ERROR(("\nSuspending flowid %d\n", flow_ring_node->flowid));
			/* delete from the active list */
			__dhd_flow_ring_delete_from_active_list(bus, flow_ring_node);
			flow_ring_node->status = FLOW_RING_STATUS_SUSPENDED;
			ringid[count] = flow_ring_node->flowid;
			count++;
			if (count == MAX_SUSPEND_REQ) {
				/* create a batch message now!! */
				dhd_prot_flow_ring_batch_suspend_request(bus->dhd, ringid, count);
				count = 0;
			}

		} else {

			/* No more scanning, break from here! */
			break;
		}
	}

	if (count) {
		dhd_prot_flow_ring_batch_suspend_request(bus->dhd, ringid, count);
	}

	DHD_FLOWRING_LIST_UNLOCK(bus->dhd->flowring_list_lock, flags);

	return;
}

void dhd_flow_ring_move_to_active_list_head(struct dhd_bus *bus, flow_ring_node_t *flow_ring_node)
{
	unsigned long flags;
	dll_t* list;

	DHD_FLOWRING_LIST_LOCK(bus->dhd->flowring_list_lock, flags);
	/* check if the node is already at head, otherwise delete it and prepend */
	list = dll_head_p(&bus->flowring_active_list);
	if (&flow_ring_node->list != list) {
		dll_delete(&flow_ring_node->list);
		dll_prepend(&bus->flowring_active_list, &flow_ring_node->list);
	}

	/* update flow ring timestamp */
	flow_ring_node->last_active_ts = OSL_SYSUPTIME();

	DHD_FLOWRING_LIST_UNLOCK(bus->dhd->flowring_list_lock, flags);

	return;
}

void dhd_flow_ring_add_to_active_list(struct dhd_bus *bus, flow_ring_node_t *flow_ring_node)
{
	unsigned long flags;

	DHD_FLOWRING_LIST_LOCK(bus->dhd->flowring_list_lock, flags);

	dll_prepend(&bus->flowring_active_list, &flow_ring_node->list);
	/* update flow ring timestamp */
	flow_ring_node->last_active_ts = OSL_SYSUPTIME();

	DHD_FLOWRING_LIST_UNLOCK(bus->dhd->flowring_list_lock, flags);

	return;
}
void __dhd_flow_ring_delete_from_active_list(struct dhd_bus *bus, flow_ring_node_t *flow_ring_node)
{
	dll_delete(&flow_ring_node->list);
}

void dhd_flow_ring_delete_from_active_list(struct dhd_bus *bus, flow_ring_node_t *flow_ring_node)
{
	unsigned long flags;

	DHD_FLOWRING_LIST_LOCK(bus->dhd->flowring_list_lock, flags);

	__dhd_flow_ring_delete_from_active_list(bus, flow_ring_node);

	DHD_FLOWRING_LIST_UNLOCK(bus->dhd->flowring_list_lock, flags);

	return;
}
#endif /* IDLE_TX_FLOW_MGMT */

int
dhdpcie_bus_clock_start(struct dhd_bus *bus)
{
	return dhdpcie_start_host_pcieclock(bus);
}

int
dhdpcie_bus_clock_stop(struct dhd_bus *bus)
{
	return dhdpcie_stop_host_pcieclock(bus);
}

int
dhdpcie_bus_disable_device(struct dhd_bus *bus)
{
	return dhdpcie_disable_device(bus);
}

int
dhdpcie_bus_enable_device(struct dhd_bus *bus)
{
	return dhdpcie_enable_device(bus);
}

int
dhdpcie_bus_alloc_resource(struct dhd_bus *bus)
{
	return dhdpcie_alloc_resource(bus);
}

void
dhdpcie_bus_free_resource(struct dhd_bus *bus)
{
	dhdpcie_free_resource(bus);
}

int
dhd_bus_request_irq(struct dhd_bus *bus)
{
	return dhdpcie_bus_request_irq(bus);
}

bool
dhdpcie_bus_dongle_attach(struct dhd_bus *bus)
{
	return dhdpcie_dongle_attach(bus);
}

int
dhd_bus_release_dongle(struct dhd_bus *bus)
{
	bool dongle_isolation;
	osl_t *osh;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus) {
		osh = bus->osh;
		ASSERT(osh);

		if (bus->dhd) {
#if defined(DEBUGGER) || defined(DHD_DSCOPE)
			debugger_close();
#endif /* DEBUGGER || DHD_DSCOPE */

			dongle_isolation = bus->dhd->dongle_isolation;
			dhdpcie_bus_release_dongle(bus, osh, dongle_isolation, TRUE);
		}
	}

	return 0;
}

int
dhdpcie_cto_cfg_init(struct dhd_bus *bus, bool enable)
{
	uint32 val;
	if (enable) {
		dhdpcie_bus_cfg_write_dword(bus, PCI_INT_MASK, 4,
			PCI_CTO_INT_MASK | PCI_SBIM_MASK_SERR);
		val = dhdpcie_bus_cfg_read_dword(bus, PCI_SPROM_CONTROL, 4);
		dhdpcie_bus_cfg_write_dword(bus, PCI_SPROM_CONTROL, 4, val | SPROM_BACKPLANE_EN);
	} else {
		dhdpcie_bus_cfg_write_dword(bus, PCI_INT_MASK, 4, 0);
		val = dhdpcie_bus_cfg_read_dword(bus, PCI_SPROM_CONTROL, 4);
		dhdpcie_bus_cfg_write_dword(bus, PCI_SPROM_CONTROL, 4, val & ~SPROM_BACKPLANE_EN);
	}
	return 0;
}

int
dhdpcie_cto_init(struct dhd_bus *bus, bool enable)
{
	if (bus->sih->buscorerev < 19) {
		DHD_INFO(("%s: Unsupported CTO, buscorerev=%d\n",
			__FUNCTION__, bus->sih->buscorerev));
		return BCME_UNSUPPORTED;
	}

	if (bus->sih->buscorerev == 19) {
		uint32 pcie_lnkst;
		si_corereg(bus->sih, bus->sih->buscoreidx,
			OFFSETOF(sbpcieregs_t, configaddr), ~0, PCI_LINK_STATUS);

		pcie_lnkst = si_corereg(bus->sih, bus->sih->buscoreidx,
			OFFSETOF(sbpcieregs_t, configdata), 0, 0);

		if (((pcie_lnkst >> PCI_LINK_SPEED_SHIFT) &
			PCI_LINK_SPEED_MASK) == PCIE_LNK_SPEED_GEN1) {
			return BCME_UNSUPPORTED;
		}
	}

	bus->cto_enable = enable;

	dhdpcie_cto_cfg_init(bus, enable);

	if (enable) {
		if (bus->cto_threshold == 0) {
			bus->cto_threshold = PCIE_CTO_TO_THRESH_DEFAULT;
		}
		si_corereg(bus->sih, bus->sih->buscoreidx,
			OFFSETOF(sbpcieregs_t, ctoctrl), ~0,
			((bus->cto_threshold << PCIE_CTO_TO_THRESHOLD_SHIFT) &
			PCIE_CTO_TO_THRESHHOLD_MASK) |
			((PCIE_CTO_CLKCHKCNT_VAL << PCIE_CTO_CLKCHKCNT_SHIFT) &
			PCIE_CTO_CLKCHKCNT_MASK) |
			PCIE_CTO_ENAB_MASK);
	} else {
		si_corereg(bus->sih, bus->sih->buscoreidx,
			OFFSETOF(sbpcieregs_t, ctoctrl), ~0, 0);
	}

	DHD_ERROR(("%s: set CTO prevention and recovery enable/disable %d\n",
		__FUNCTION__, bus->cto_enable));

	return 0;
}

static int
dhdpcie_cto_error_recovery(struct dhd_bus *bus)
{
	uint32 pci_intmask, err_status;
	uint8 i = 0;
	uint32 val;

	pci_intmask = dhdpcie_bus_cfg_read_dword(bus, PCI_INT_MASK, 4);
	dhdpcie_bus_cfg_write_dword(bus, PCI_INT_MASK, 4, pci_intmask & ~PCI_CTO_INT_MASK);

	DHD_OS_WAKE_LOCK(bus->dhd);

	DHD_ERROR(("--- CTO Triggered --- %d\n", bus->pwr_req_ref));

	/*
	 * DAR still accessible
	 */
	dhd_bus_dump_dar_registers(bus);

	/* reset backplane */
	val = dhdpcie_bus_cfg_read_dword(bus, PCI_SPROM_CONTROL, 4);
	dhdpcie_bus_cfg_write_dword(bus, PCI_SPROM_CONTROL, 4, val | SPROM_CFG_TO_SB_RST);

	/* clear timeout error */
	while (1) {
		err_status =  si_corereg(bus->sih, bus->sih->buscoreidx,
			DAR_ERRLOG(bus->sih->buscorerev),
			0, 0);
		if (err_status & PCIE_CTO_ERR_MASK) {
			si_corereg(bus->sih, bus->sih->buscoreidx,
					DAR_ERRLOG(bus->sih->buscorerev),
					~0, PCIE_CTO_ERR_MASK);
		} else {
			break;
		}
		OSL_DELAY(CTO_TO_CLEAR_WAIT_MS * 1000);
		i++;
		if (i > CTO_TO_CLEAR_WAIT_MAX_CNT) {
			DHD_ERROR(("cto recovery fail\n"));

			DHD_OS_WAKE_UNLOCK(bus->dhd);
			return BCME_ERROR;
		}
	}

	/* clear interrupt status */
	dhdpcie_bus_cfg_write_dword(bus, PCI_INT_STATUS, 4, PCI_CTO_INT_MASK);

	/* Halt ARM & remove reset */
	/* TBD : we can add ARM Halt here in case */

	/* reset SPROM_CFG_TO_SB_RST */
	val = dhdpcie_bus_cfg_read_dword(bus, PCI_SPROM_CONTROL, 4);

	DHD_ERROR(("cto recovery reset 0x%x:SPROM_CFG_TO_SB_RST(0x%x) 0x%x\n",
		PCI_SPROM_CONTROL, SPROM_CFG_TO_SB_RST, val));
	dhdpcie_bus_cfg_write_dword(bus, PCI_SPROM_CONTROL, 4, val & ~SPROM_CFG_TO_SB_RST);

	val = dhdpcie_bus_cfg_read_dword(bus, PCI_SPROM_CONTROL, 4);
	DHD_ERROR(("cto recovery success 0x%x:SPROM_CFG_TO_SB_RST(0x%x) 0x%x\n",
		PCI_SPROM_CONTROL, SPROM_CFG_TO_SB_RST, val));

	DHD_OS_WAKE_UNLOCK(bus->dhd);

	return BCME_OK;
}

void
dhdpcie_ssreset_dis_enum_rst(struct dhd_bus *bus)
{
	uint32 val;

	val = dhdpcie_bus_cfg_read_dword(bus, PCIE_CFG_SUBSYSTEM_CONTROL, 4);
	dhdpcie_bus_cfg_write_dword(bus, PCIE_CFG_SUBSYSTEM_CONTROL, 4,
		val | (0x1 << PCIE_SSRESET_DIS_ENUM_RST_BIT));
}

#if defined(DBG_PKT_MON) || defined(DHD_PKT_LOGGING)
static int
dhdpcie_init_d11status(struct dhd_bus *bus)
{
	uint32 addr;
	uint32 flags2;
	int ret = 0;

	if (bus->pcie_sh->flags2 & PCIE_SHARED2_D2H_D11_TX_STATUS) {
		flags2 = bus->pcie_sh->flags2;
		addr = bus->shared_addr + OFFSETOF(pciedev_shared_t, flags2);
		flags2 |= PCIE_SHARED2_H2D_D11_TX_STATUS;
		ret = dhdpcie_bus_membytes(bus, TRUE, addr,
			(uint8 *)&flags2, sizeof(flags2));
		if (ret < 0) {
			DHD_ERROR(("%s: update flag bit (H2D_D11_TX_STATUS) failed\n",
				__FUNCTION__));
			return ret;
		}
		bus->pcie_sh->flags2 = flags2;
		bus->dhd->d11_tx_status = TRUE;
	}
	return ret;
}

#else
static int
dhdpcie_init_d11status(struct dhd_bus *bus)
{
	return 0;
}
#endif /* DBG_PKT_MON || DHD_PKT_LOGGING */

#ifdef BCMPCIE_OOB_HOST_WAKE
int
dhd_bus_oob_intr_register(dhd_pub_t *dhdp)
{
	return dhdpcie_oob_intr_register(dhdp->bus);
}

void
dhd_bus_oob_intr_unregister(dhd_pub_t *dhdp)
{
	dhdpcie_oob_intr_unregister(dhdp->bus);
}

void
dhd_bus_oob_intr_set(dhd_pub_t *dhdp, bool enable)
{
	dhdpcie_oob_intr_set(dhdp->bus, enable);
}
#endif /* BCMPCIE_OOB_HOST_WAKE */

bool
dhdpcie_bus_get_pcie_hostready_supported(dhd_bus_t *bus)
{
	return bus->dhd->d2h_hostrdy_supported;
}

void
dhd_pcie_dump_core_regs(dhd_pub_t * pub, uint32 index, uint32 first_addr, uint32 last_addr)
{
	dhd_bus_t *bus = pub->bus;
	uint32	coreoffset = index << 12;
	uint32	core_addr = SI_ENUM_BASE(bus->sih) + coreoffset;
	uint32 value;

	while (first_addr <= last_addr) {
		core_addr = SI_ENUM_BASE(bus->sih) + coreoffset + first_addr;
		if (serialized_backplane_access(bus, core_addr, 4, &value, TRUE) != BCME_OK) {
			DHD_ERROR(("Invalid size/addr combination \n"));
		}
		DHD_ERROR(("[0x%08x]: 0x%08x\n", core_addr, value));
		first_addr = first_addr + 4;
	}
}

bool
dhdpcie_bus_get_pcie_hwa_supported(dhd_bus_t *bus)
{
	if (!bus->dhd)
		return FALSE;
	else if (bus->hwa_enab_bmap) {
		return bus->dhd->hwa_enable;
	} else {
		return FALSE;
	}
}

bool
dhdpcie_bus_get_pcie_idma_supported(dhd_bus_t *bus)
{
	if (!bus->dhd)
		return FALSE;
	else if (bus->idma_enabled) {
		return bus->dhd->idma_enable;
	} else {
		return FALSE;
	}
}

bool
dhdpcie_bus_get_pcie_ifrm_supported(dhd_bus_t *bus)
{
	if (!bus->dhd)
		return FALSE;
	else if (bus->ifrm_enabled) {
		return bus->dhd->ifrm_enable;
	} else {
		return FALSE;
	}
}

bool
dhdpcie_bus_get_pcie_dar_supported(dhd_bus_t *bus)
{
	if (!bus->dhd) {
		return FALSE;
	} else if (bus->dar_enabled) {
		return bus->dhd->dar_enable;
	} else {
		return FALSE;
	}
}

void
dhdpcie_bus_enab_pcie_dw(dhd_bus_t *bus, uint8 dw_option)
{
	DHD_ERROR(("ENABLING DW:%d\n", dw_option));
	bus->dw_option = dw_option;
}

void
dhd_bus_dump_trap_info(dhd_bus_t *bus, struct bcmstrbuf *strbuf)
{
	trap_t *tr = &bus->dhd->last_trap_info;
	bcm_bprintf(strbuf,
		"\nTRAP type 0x%x @ epc 0x%x, cpsr 0x%x, spsr 0x%x, sp 0x%x,"
		" lp 0x%x, rpc 0x%x"
		"\nTrap offset 0x%x, r0 0x%x, r1 0x%x, r2 0x%x, r3 0x%x, "
		"r4 0x%x, r5 0x%x, r6 0x%x, r7 0x%x, r8 0x%x, r9 0x%x, "
		"r10 0x%x, r11 0x%x, r12 0x%x\n\n",
		ltoh32(tr->type), ltoh32(tr->epc), ltoh32(tr->cpsr), ltoh32(tr->spsr),
		ltoh32(tr->r13), ltoh32(tr->r14), ltoh32(tr->pc),
		ltoh32(bus->pcie_sh->trap_addr),
		ltoh32(tr->r0), ltoh32(tr->r1), ltoh32(tr->r2), ltoh32(tr->r3),
		ltoh32(tr->r4), ltoh32(tr->r5), ltoh32(tr->r6), ltoh32(tr->r7),
		ltoh32(tr->r8), ltoh32(tr->r9), ltoh32(tr->r10),
		ltoh32(tr->r11), ltoh32(tr->r12));
}

int
dhd_bus_readwrite_bp_addr(dhd_pub_t *dhdp, uint addr, uint size, uint* data, bool read)
{
	int bcmerror = 0;
	struct dhd_bus *bus = dhdp->bus;

	if (serialized_backplane_access(bus, addr, size, data, read) != BCME_OK) {
			DHD_ERROR(("Invalid size/addr combination \n"));
			bcmerror = BCME_ERROR;
	}

	return bcmerror;
}

int
dhd_get_idletime(dhd_pub_t *dhd)
{
	return dhd->bus->idletime;
}

static INLINE void
dhd_sbreg_op(dhd_pub_t *dhd, uint addr, uint *val, bool read)
{
	OSL_DELAY(1);
	if (serialized_backplane_access(dhd->bus, addr, sizeof(uint), val, read) != BCME_OK) {
		DHD_ERROR(("sbreg: Invalid uint addr: 0x%x \n", addr));
	} else {
		DHD_ERROR(("sbreg: addr:0x%x val:0x%x read:%d\n", addr, *val, read));
	}
	return;
}

#ifdef DHD_SSSR_DUMP
static int
dhdpcie_get_sssr_fifo_dump(dhd_pub_t *dhd, uint *buf, uint fifo_size,
	uint addr_reg, uint data_reg)
{
	uint addr;
	uint val = 0;
	int i;

	DHD_ERROR(("%s\n", __FUNCTION__));

	if (!buf) {
		DHD_ERROR(("%s: buf is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (!fifo_size) {
		DHD_ERROR(("%s: fifo_size is 0\n", __FUNCTION__));
		return BCME_ERROR;
	}

	/* Set the base address offset to 0 */
	addr = addr_reg;
	val = 0;
	dhd_sbreg_op(dhd, addr, &val, FALSE);

	addr = data_reg;
	/* Read 4 bytes at once and loop for fifo_size / 4 */
	for (i = 0; i < fifo_size / 4; i++) {
		if (serialized_backplane_access(dhd->bus, addr,
				sizeof(uint), &val, TRUE) != BCME_OK) {
			DHD_ERROR(("%s: error in serialized_backplane_access\n", __FUNCTION__));
			return BCME_ERROR;
		}
		buf[i] = val;
		OSL_DELAY(1);
	}
	return BCME_OK;
}

static int
dhdpcie_get_sssr_dig_dump(dhd_pub_t *dhd, uint *buf, uint fifo_size,
	uint addr_reg)
{
	uint addr;
	uint val = 0;
	int i;
	si_t *sih = dhd->bus->sih;

	DHD_ERROR(("%s\n", __FUNCTION__));

	if (!buf) {
		DHD_ERROR(("%s: buf is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (!fifo_size) {
		DHD_ERROR(("%s: fifo_size is 0\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (addr_reg) {

		if ((!dhd->sssr_reg_info.vasip_regs.vasip_sr_size) &&
			dhd->sssr_reg_info.length > OFFSETOF(sssr_reg_info_v1_t, dig_mem_info)) {
			int err = dhdpcie_bus_membytes(dhd->bus, FALSE, addr_reg, (uint8 *)buf,
				fifo_size);
			if (err != BCME_OK) {
				DHD_ERROR(("%s: Error reading dig dump from dongle !\n",
					__FUNCTION__));
			}
		} else {
			/* Check if vasip clk is disabled, if yes enable it */
			addr = dhd->sssr_reg_info.vasip_regs.wrapper_regs.ioctrl;
			dhd_sbreg_op(dhd, addr, &val, TRUE);
			if (!val) {
				val = 1;
				dhd_sbreg_op(dhd, addr, &val, FALSE);
			}

			addr = addr_reg;
			/* Read 4 bytes at once and loop for fifo_size / 4 */
			for (i = 0; i < fifo_size / 4; i++, addr += 4) {
				if (serialized_backplane_access(dhd->bus, addr, sizeof(uint),
					&val, TRUE) != BCME_OK) {
					DHD_ERROR(("%s: Invalid uint addr: 0x%x \n", __FUNCTION__,
						addr));
					return BCME_ERROR;
				}
				buf[i] = val;
				OSL_DELAY(1);
			}
		}
	} else {
		uint cur_coreid;
		uint chipc_corerev;
		chipcregs_t *chipcregs;

		/* Save the current core */
		cur_coreid = si_coreid(sih);

		/* Switch to ChipC */
		chipcregs = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0);

		chipc_corerev = si_corerev(sih);

		if ((chipc_corerev == 64) || (chipc_corerev == 65)) {
			W_REG(si_osh(sih), &chipcregs->sr_memrw_addr, 0);

			/* Read 4 bytes at once and loop for fifo_size / 4 */
			for (i = 0; i < fifo_size / 4; i++) {
				buf[i] = R_REG(si_osh(sih), &chipcregs->sr_memrw_data);
				OSL_DELAY(1);
			}
		}

		/* Switch back to the original core */
		si_setcore(sih, cur_coreid, 0);
	}

	return BCME_OK;
}

#if defined(EWP_ETD_PRSRV_LOGS)
void
dhdpcie_get_etd_preserve_logs(dhd_pub_t *dhd,
		uint8 *ext_trap_data, void *event_decode_data)
{
	hnd_ext_trap_hdr_t *hdr = NULL;
	bcm_tlv_t *tlv;
	eventlog_trapdata_info_t *etd_evtlog = NULL;
	eventlog_trap_buf_info_t *evtlog_buf_arr = NULL;
	uint arr_size = 0;
	int i = 0;
	int err = 0;
	uint32 seqnum = 0;

	if (!ext_trap_data || !event_decode_data || !dhd)
		return;

	if (!dhd->concise_dbg_buf)
		return;

	/* First word is original trap_data, skip */
	ext_trap_data += sizeof(uint32);

	hdr = (hnd_ext_trap_hdr_t *)ext_trap_data;
	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_LOG_DATA);
	if (tlv) {
		uint32 baseaddr = 0;
		uint32 endaddr = dhd->bus->dongle_ram_base + dhd->bus->ramsize - 4;

		etd_evtlog = (eventlog_trapdata_info_t *)tlv->data;
		DHD_ERROR(("%s: etd_evtlog tlv found, num_elements=%x; "
			"seq_num=%x; log_arr_addr=%x\n", __FUNCTION__,
			(etd_evtlog->num_elements),
			ntoh32(etd_evtlog->seq_num), (etd_evtlog->log_arr_addr)));
		arr_size = (uint32)sizeof(*evtlog_buf_arr) * (etd_evtlog->num_elements);
		if (!arr_size) {
			DHD_ERROR(("%s: num event logs is zero! \n", __FUNCTION__));
			return;
		}
		evtlog_buf_arr = MALLOCZ(dhd->osh, arr_size);
		if (!evtlog_buf_arr) {
			DHD_ERROR(("%s: out of memory !\n",	__FUNCTION__));
			return;
		}

		/* boundary check */
		baseaddr = etd_evtlog->log_arr_addr;
		if ((baseaddr < dhd->bus->dongle_ram_base) ||
			((baseaddr + arr_size) > endaddr)) {
			DHD_ERROR(("%s: Error reading invalid address\n",
				__FUNCTION__));
			goto err;
		}

		/* read the eventlog_trap_buf_info_t array from dongle memory */
		err = dhdpcie_bus_membytes(dhd->bus, FALSE,
				(ulong)(etd_evtlog->log_arr_addr),
				(uint8 *)evtlog_buf_arr, arr_size);
		if (err != BCME_OK) {
			DHD_ERROR(("%s: Error reading event log array from dongle !\n",
				__FUNCTION__));
			goto err;
		}
		/* ntoh is required only for seq_num, because in the original
		* case of event logs from info ring, it is sent from dongle in that way
		* so for ETD also dongle follows same convention
		*/
		seqnum = ntoh32(etd_evtlog->seq_num);
		memset(dhd->concise_dbg_buf, 0, CONCISE_DUMP_BUFLEN);
		for (i = 0; i < (etd_evtlog->num_elements); ++i) {
			/* boundary check */
			baseaddr = evtlog_buf_arr[i].buf_addr;
			if ((baseaddr < dhd->bus->dongle_ram_base) ||
				((baseaddr + evtlog_buf_arr[i].len) > endaddr)) {
				DHD_ERROR(("%s: Error reading invalid address\n",
					__FUNCTION__));
				goto err;
			}
			/* read each individual event log buf from dongle memory */
			err = dhdpcie_bus_membytes(dhd->bus, FALSE,
					((ulong)evtlog_buf_arr[i].buf_addr),
					dhd->concise_dbg_buf, (evtlog_buf_arr[i].len));
			if (err != BCME_OK) {
				DHD_ERROR(("%s: Error reading event log buffer from dongle !\n",
					__FUNCTION__));
				goto err;
			}
			dhd_dbg_msgtrace_log_parser(dhd, dhd->concise_dbg_buf,
				event_decode_data, (evtlog_buf_arr[i].len),
				FALSE, hton32(seqnum));
			++seqnum;
		}
err:
		MFREE(dhd->osh, evtlog_buf_arr, arr_size);
	} else {
		DHD_ERROR(("%s: Error getting trap log data in ETD !\n", __FUNCTION__));
	}
}
#endif /* BCMPCIE && DHD_LOG_DUMP */

static uint32
dhdpcie_resume_chipcommon_powerctrl(dhd_pub_t *dhd, uint32 reg_val)
{
	uint addr;
	uint val = 0;

	DHD_ERROR(("%s\n", __FUNCTION__));

	/* conditionally clear bits [11:8] of PowerCtrl */
	addr = dhd->sssr_reg_info.chipcommon_regs.base_regs.powerctrl;
	dhd_sbreg_op(dhd, addr, &val, TRUE);
	if (!(val & dhd->sssr_reg_info.chipcommon_regs.base_regs.powerctrl_mask)) {
		addr = dhd->sssr_reg_info.chipcommon_regs.base_regs.powerctrl;
		dhd_sbreg_op(dhd, addr, &reg_val, FALSE);
	}
	return BCME_OK;
}

static uint32
dhdpcie_suspend_chipcommon_powerctrl(dhd_pub_t *dhd)
{
	uint addr;
	uint val = 0, reg_val = 0;

	DHD_ERROR(("%s\n", __FUNCTION__));

	/* conditionally clear bits [11:8] of PowerCtrl */
	addr = dhd->sssr_reg_info.chipcommon_regs.base_regs.powerctrl;
	dhd_sbreg_op(dhd, addr, &reg_val, TRUE);
	if (reg_val & dhd->sssr_reg_info.chipcommon_regs.base_regs.powerctrl_mask) {
		addr = dhd->sssr_reg_info.chipcommon_regs.base_regs.powerctrl;
		val = 0;
		dhd_sbreg_op(dhd, addr, &val, FALSE);
	}
	return reg_val;
}

static int
dhdpcie_clear_intmask_and_timer(dhd_pub_t *dhd)
{
	uint addr;
	uint val;

	DHD_ERROR(("%s\n", __FUNCTION__));

	/* clear chipcommon intmask */
	addr = dhd->sssr_reg_info.chipcommon_regs.base_regs.intmask;
	val = 0x0;
	dhd_sbreg_op(dhd, addr, &val, FALSE);

	/* clear PMUIntMask0 */
	addr = dhd->sssr_reg_info.pmu_regs.base_regs.pmuintmask0;
	val = 0x0;
	dhd_sbreg_op(dhd, addr, &val, FALSE);

	/* clear PMUIntMask1 */
	addr = dhd->sssr_reg_info.pmu_regs.base_regs.pmuintmask1;
	val = 0x0;
	dhd_sbreg_op(dhd, addr, &val, FALSE);

	/* clear res_req_timer */
	addr = dhd->sssr_reg_info.pmu_regs.base_regs.resreqtimer;
	val = 0x0;
	dhd_sbreg_op(dhd, addr, &val, FALSE);

	/* clear macresreqtimer */
	addr = dhd->sssr_reg_info.pmu_regs.base_regs.macresreqtimer;
	val = 0x0;
	dhd_sbreg_op(dhd, addr, &val, FALSE);

	/* clear macresreqtimer1 */
	addr = dhd->sssr_reg_info.pmu_regs.base_regs.macresreqtimer1;
	val = 0x0;
	dhd_sbreg_op(dhd, addr, &val, FALSE);

	/* clear VasipClkEn */
	if (dhd->sssr_reg_info.vasip_regs.vasip_sr_size) {
		addr = dhd->sssr_reg_info.vasip_regs.wrapper_regs.ioctrl;
		val = 0x0;
		dhd_sbreg_op(dhd, addr, &val, FALSE);
	}

	return BCME_OK;
}

static void
dhdpcie_update_d11_status_from_trapdata(dhd_pub_t *dhd)
{
#define TRAP_DATA_MAIN_CORE_BIT_MASK	(1 << 1)
#define TRAP_DATA_AUX_CORE_BIT_MASK	(1 << 4)
	uint trap_data_mask[MAX_NUM_D11CORES] =
		{TRAP_DATA_MAIN_CORE_BIT_MASK, TRAP_DATA_AUX_CORE_BIT_MASK};
	int i;
	/* Apply only for 4375 chip */
	if (dhd_bus_chip_id(dhd) == BCM4375_CHIP_ID) {
		for (i = 0; i < MAX_NUM_D11CORES; i++) {
			if (dhd->sssr_d11_outofreset[i] &&
				(dhd->dongle_trap_data & trap_data_mask[i])) {
				dhd->sssr_d11_outofreset[i] = TRUE;
			} else {
				dhd->sssr_d11_outofreset[i] = FALSE;
			}
			DHD_ERROR(("%s: sssr_d11_outofreset[%d] : %d after AND with "
				"trap_data:0x%x-0x%x\n",
				__FUNCTION__, i, dhd->sssr_d11_outofreset[i],
				dhd->dongle_trap_data, trap_data_mask[i]));
		}
	}
}

static int
dhdpcie_d11_check_outofreset(dhd_pub_t *dhd)
{
	int i;
	uint addr;
	uint val = 0;

	DHD_ERROR(("%s\n", __FUNCTION__));

	for (i = 0; i < MAX_NUM_D11CORES; i++) {
		/* Check if bit 0 of resetctrl is cleared */
		addr = dhd->sssr_reg_info.mac_regs[i].wrapper_regs.resetctrl;
		if (!addr) {
			DHD_ERROR(("%s: skipping for core[%d] as 'addr' is NULL\n",
				__FUNCTION__, i));
			continue;
		}
		dhd_sbreg_op(dhd, addr, &val, TRUE);
		if (!(val & 1)) {
			dhd->sssr_d11_outofreset[i] = TRUE;
		} else {
			dhd->sssr_d11_outofreset[i] = FALSE;
		}
		DHD_ERROR(("%s: sssr_d11_outofreset[%d] : %d\n",
			__FUNCTION__, i, dhd->sssr_d11_outofreset[i]));
	}
	dhdpcie_update_d11_status_from_trapdata(dhd);

	return BCME_OK;
}

static int
dhdpcie_d11_clear_clk_req(dhd_pub_t *dhd)
{
	int i;
	uint addr;
	uint val = 0;

	DHD_ERROR(("%s\n", __FUNCTION__));

	for (i = 0; i < MAX_NUM_D11CORES; i++) {
		if (dhd->sssr_d11_outofreset[i]) {
			/* clear request clk only if itopoobb is non zero */
			addr = dhd->sssr_reg_info.mac_regs[i].wrapper_regs.itopoobb;
			dhd_sbreg_op(dhd, addr, &val, TRUE);
			if (val != 0) {
				/* clear clockcontrolstatus */
				addr = dhd->sssr_reg_info.mac_regs[i].base_regs.clockcontrolstatus;
				val =
				dhd->sssr_reg_info.mac_regs[i].base_regs.clockcontrolstatus_val;
				dhd_sbreg_op(dhd, addr, &val, FALSE);
			}
		}
	}
	return BCME_OK;
}

static int
dhdpcie_arm_clear_clk_req(dhd_pub_t *dhd)
{
	uint addr;
	uint val = 0;

	DHD_ERROR(("%s\n", __FUNCTION__));

	/* Check if bit 0 of resetctrl is cleared */
	addr = dhd->sssr_reg_info.arm_regs.wrapper_regs.resetctrl;
	dhd_sbreg_op(dhd, addr, &val, TRUE);
	if (!(val & 1)) {
		/* clear request clk only if itopoobb is non zero */
		addr = dhd->sssr_reg_info.arm_regs.wrapper_regs.itopoobb;
		dhd_sbreg_op(dhd, addr, &val, TRUE);
		if (val != 0) {
			/* clear clockcontrolstatus */
			addr = dhd->sssr_reg_info.arm_regs.base_regs.clockcontrolstatus;
			val = dhd->sssr_reg_info.arm_regs.base_regs.clockcontrolstatus_val;
			dhd_sbreg_op(dhd, addr, &val, FALSE);
		}
	}
	return BCME_OK;
}

static int
dhdpcie_pcie_clear_clk_req(dhd_pub_t *dhd)
{
	uint addr;
	uint val = 0;

	DHD_ERROR(("%s\n", __FUNCTION__));

	/* clear request clk only if itopoobb is non zero */
	addr = dhd->sssr_reg_info.pcie_regs.wrapper_regs.itopoobb;
	dhd_sbreg_op(dhd, addr, &val, TRUE);
	if (val) {
		/* clear clockcontrolstatus */
		addr = dhd->sssr_reg_info.pcie_regs.base_regs.clockcontrolstatus;
		val = dhd->sssr_reg_info.pcie_regs.base_regs.clockcontrolstatus_val;
		dhd_sbreg_op(dhd, addr, &val, FALSE);
	}
	return BCME_OK;
}

static int
dhdpcie_pcie_send_ltrsleep(dhd_pub_t *dhd)
{
	uint addr;
	uint val = 0;

	DHD_ERROR(("%s\n", __FUNCTION__));

	addr = dhd->sssr_reg_info.pcie_regs.base_regs.ltrstate;
	val = LTR_ACTIVE;
	dhd_sbreg_op(dhd, addr, &val, FALSE);

	val = LTR_SLEEP;
	dhd_sbreg_op(dhd, addr, &val, FALSE);

	return BCME_OK;
}

static int
dhdpcie_clear_clk_req(dhd_pub_t *dhd)
{
	DHD_ERROR(("%s\n", __FUNCTION__));

	dhdpcie_arm_clear_clk_req(dhd);

	dhdpcie_d11_clear_clk_req(dhd);

	dhdpcie_pcie_clear_clk_req(dhd);

	return BCME_OK;
}

static int
dhdpcie_bring_d11_outofreset(dhd_pub_t *dhd)
{
	int i;
	uint addr;
	uint val = 0;

	DHD_ERROR(("%s\n", __FUNCTION__));

	for (i = 0; i < MAX_NUM_D11CORES; i++) {
		if (dhd->sssr_d11_outofreset[i]) {
			/* disable core by setting bit 0 */
			addr = dhd->sssr_reg_info.mac_regs[i].wrapper_regs.resetctrl;
			val = 1;
			dhd_sbreg_op(dhd, addr, &val, FALSE);
			OSL_DELAY(6000);

			addr = dhd->sssr_reg_info.mac_regs[i].wrapper_regs.ioctrl;
			val = dhd->sssr_reg_info.mac_regs[0].wrapper_regs.ioctrl_resetseq_val[0];
			dhd_sbreg_op(dhd, addr, &val, FALSE);

			val = dhd->sssr_reg_info.mac_regs[0].wrapper_regs.ioctrl_resetseq_val[1];
			dhd_sbreg_op(dhd, addr, &val, FALSE);

			/* enable core by clearing bit 0 */
			addr = dhd->sssr_reg_info.mac_regs[i].wrapper_regs.resetctrl;
			val = 0;
			dhd_sbreg_op(dhd, addr, &val, FALSE);

			addr = dhd->sssr_reg_info.mac_regs[i].wrapper_regs.ioctrl;
			val = dhd->sssr_reg_info.mac_regs[0].wrapper_regs.ioctrl_resetseq_val[2];
			dhd_sbreg_op(dhd, addr, &val, FALSE);

			val = dhd->sssr_reg_info.mac_regs[0].wrapper_regs.ioctrl_resetseq_val[3];
			dhd_sbreg_op(dhd, addr, &val, FALSE);

			val = dhd->sssr_reg_info.mac_regs[0].wrapper_regs.ioctrl_resetseq_val[4];
			dhd_sbreg_op(dhd, addr, &val, FALSE);
		}
	}
	return BCME_OK;
}

static int
dhdpcie_sssr_dump_get_before_sr(dhd_pub_t *dhd)
{
	int i;

	DHD_ERROR(("%s\n", __FUNCTION__));

	for (i = 0; i < MAX_NUM_D11CORES; i++) {
		if (dhd->sssr_d11_outofreset[i]) {
			dhdpcie_get_sssr_fifo_dump(dhd, dhd->sssr_d11_before[i],
				dhd->sssr_reg_info.mac_regs[i].sr_size,
				dhd->sssr_reg_info.mac_regs[i].base_regs.xmtaddress,
				dhd->sssr_reg_info.mac_regs[i].base_regs.xmtdata);
		}
	}

	if (dhd->sssr_reg_info.vasip_regs.vasip_sr_size) {
		dhdpcie_get_sssr_dig_dump(dhd, dhd->sssr_dig_buf_before,
			dhd->sssr_reg_info.vasip_regs.vasip_sr_size,
			dhd->sssr_reg_info.vasip_regs.vasip_sr_addr);
	} else if ((dhd->sssr_reg_info.length > OFFSETOF(sssr_reg_info_v1_t, dig_mem_info)) &&
		dhd->sssr_reg_info.dig_mem_info.dig_sr_addr) {
		dhdpcie_get_sssr_dig_dump(dhd, dhd->sssr_dig_buf_before,
			dhd->sssr_reg_info.dig_mem_info.dig_sr_size,
			dhd->sssr_reg_info.dig_mem_info.dig_sr_addr);
	}

	return BCME_OK;
}

static int
dhdpcie_sssr_dump_get_after_sr(dhd_pub_t *dhd)
{
	int i;

	DHD_ERROR(("%s\n", __FUNCTION__));

	for (i = 0; i < MAX_NUM_D11CORES; i++) {
		if (dhd->sssr_d11_outofreset[i]) {
			dhdpcie_get_sssr_fifo_dump(dhd, dhd->sssr_d11_after[i],
				dhd->sssr_reg_info.mac_regs[i].sr_size,
				dhd->sssr_reg_info.mac_regs[i].base_regs.xmtaddress,
				dhd->sssr_reg_info.mac_regs[i].base_regs.xmtdata);
		}
	}

	if (dhd->sssr_reg_info.vasip_regs.vasip_sr_size) {
		dhdpcie_get_sssr_dig_dump(dhd, dhd->sssr_dig_buf_after,
			dhd->sssr_reg_info.vasip_regs.vasip_sr_size,
			dhd->sssr_reg_info.vasip_regs.vasip_sr_addr);
	} else if ((dhd->sssr_reg_info.length > OFFSETOF(sssr_reg_info_v1_t, dig_mem_info)) &&
		dhd->sssr_reg_info.dig_mem_info.dig_sr_addr) {
		dhdpcie_get_sssr_dig_dump(dhd, dhd->sssr_dig_buf_after,
			dhd->sssr_reg_info.dig_mem_info.dig_sr_size,
			dhd->sssr_reg_info.dig_mem_info.dig_sr_addr);
	}

	return BCME_OK;
}

int
dhdpcie_sssr_dump(dhd_pub_t *dhd)
{
	uint32 powerctrl_val;

	if (!dhd->sssr_inited) {
		DHD_ERROR(("%s: SSSR not inited\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (dhd->bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link is down\n", __FUNCTION__));
		return BCME_ERROR;
	}

	dhdpcie_d11_check_outofreset(dhd);

	DHD_ERROR(("%s: Collecting Dump before SR\n", __FUNCTION__));
	if (dhdpcie_sssr_dump_get_before_sr(dhd) != BCME_OK) {
		DHD_ERROR(("%s: dhdpcie_sssr_dump_get_before_sr failed\n", __FUNCTION__));
		return BCME_ERROR;
	}

	dhdpcie_clear_intmask_and_timer(dhd);
	powerctrl_val = dhdpcie_suspend_chipcommon_powerctrl(dhd);
	dhdpcie_clear_clk_req(dhd);
	dhdpcie_pcie_send_ltrsleep(dhd);

	/* Wait for some time before Restore */
	OSL_DELAY(6000);

	dhdpcie_resume_chipcommon_powerctrl(dhd, powerctrl_val);
	dhdpcie_bring_d11_outofreset(dhd);

	DHD_ERROR(("%s: Collecting Dump after SR\n", __FUNCTION__));
	if (dhdpcie_sssr_dump_get_after_sr(dhd) != BCME_OK) {
		DHD_ERROR(("%s: dhdpcie_sssr_dump_get_after_sr failed\n", __FUNCTION__));
		return BCME_ERROR;
	}
	dhd->sssr_dump_collected = TRUE;
	dhd_write_sssr_dump(dhd, SSSR_DUMP_MODE_SSSR);

	return BCME_OK;
}

static int
dhdpcie_fis_trigger(dhd_pub_t *dhd)
{
	if (!dhd->sssr_inited) {
		DHD_ERROR(("%s: SSSR not inited\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (dhd->bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link is down\n", __FUNCTION__));
		return BCME_ERROR;
	}

	/* Trigger FIS */
	si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
		DAR_FIS_CTRL(dhd->bus->sih->buscorerev), ~0, DAR_FIS_START_MASK);
	OSL_DELAY(100 * 1000);

	return BCME_OK;
}

int
dhd_bus_fis_trigger(dhd_pub_t *dhd)
{
	return dhdpcie_fis_trigger(dhd);
}

static int
dhdpcie_fis_dump(dhd_pub_t *dhd)
{
	int i;

	if (!dhd->sssr_inited) {
		DHD_ERROR(("%s: SSSR not inited\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (dhd->bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link is down\n", __FUNCTION__));
		return BCME_ERROR;
	}

	/* bring up all pmu resources */
	PMU_REG(dhd->bus->sih, min_res_mask, ~0,
		PMU_REG(dhd->bus->sih, max_res_mask, 0, 0));
	OSL_DELAY(10 * 1000);

	for (i = 0; i < MAX_NUM_D11CORES; i++) {
		dhd->sssr_d11_outofreset[i] = TRUE;
	}

	dhdpcie_bring_d11_outofreset(dhd);
	OSL_DELAY(6000);

	/* clear FIS Done */
	PMU_REG(dhd->bus->sih, fis_ctrl_status, PMU_CLEAR_FIS_DONE_MASK, PMU_CLEAR_FIS_DONE_MASK);

	dhdpcie_d11_check_outofreset(dhd);

	DHD_ERROR(("%s: Collecting Dump after SR\n", __FUNCTION__));
	if (dhdpcie_sssr_dump_get_after_sr(dhd) != BCME_OK) {
		DHD_ERROR(("%s: dhdpcie_sssr_dump_get_after_sr failed\n", __FUNCTION__));
		return BCME_ERROR;
	}

	dhd_write_sssr_dump(dhd, SSSR_DUMP_MODE_FIS);

	return BCME_OK;
}

int
dhd_bus_fis_dump(dhd_pub_t *dhd)
{
	return dhdpcie_fis_dump(dhd);
}
#endif /* DHD_SSSR_DUMP */

#ifdef DHD_WAKE_STATUS
wake_counts_t*
dhd_bus_get_wakecount(dhd_pub_t *dhd)
{
	return &dhd->bus->wake_counts;
}
int
dhd_bus_get_bus_wake(dhd_pub_t *dhd)
{
	return bcmpcie_set_get_wake(dhd->bus, 0);
}
#endif /* DHD_WAKE_STATUS */

/* Writes random number(s) to the TCM. FW upon initialization reads this register
 * to fetch the random number, and uses it to randomize heap address space layout.
 */
static int
dhdpcie_wrt_rnd(struct dhd_bus *bus)
{
	bcm_rand_metadata_t rnd_data;
	uint8 rand_buf[BCM_ENTROPY_HOST_NBYTES];
	uint32 count = BCM_ENTROPY_HOST_NBYTES;
	int ret = 0;
	uint32 addr = bus->dongle_ram_base + (bus->ramsize - BCM_NVRAM_OFFSET_TCM) -
		((bus->nvram_csm & 0xffff)* BCM_NVRAM_IMG_COMPRS_FACTOR + sizeof(rnd_data));

	memset(rand_buf, 0, BCM_ENTROPY_HOST_NBYTES);
	rnd_data.signature = htol32(BCM_NVRAM_RNG_SIGNATURE);
	rnd_data.count = htol32(count);
	/* write the metadata about random number */
	dhdpcie_bus_membytes(bus, TRUE, addr, (uint8 *)&rnd_data, sizeof(rnd_data));
	/* scale back by number of random number counts */
	addr -= count;

#ifdef DHD_RND_DEBUG
	bus->dhd->rnd_buf = NULL;
	/* get random contents from file */
	ret = dhd_get_rnd_info(bus->dhd);
	if (bus->dhd->rnd_buf) {
		/* write file contents to TCM */
		DHD_ERROR(("%s: use stored .rnd.in content\n", __FUNCTION__));
		dhdpcie_bus_membytes(bus, TRUE, addr, bus->dhd->rnd_buf, bus->dhd->rnd_len);

		/* Dump random content to out file */
		dhd_dump_rnd_info(bus->dhd, bus->dhd->rnd_buf, bus->dhd->rnd_len);

		/* bus->dhd->rnd_buf is allocated in dhd_get_rnd_info, free here */
		MFREE(bus->dhd->osh, bus->dhd->rnd_buf, bus->dhd->rnd_len);
		bus->dhd->rnd_buf = NULL;
		return BCME_OK;
	}
#endif /* DHD_RND_DEBUG */

	/* Now get & write the random number(s) */
	ret = dhd_get_random_bytes(rand_buf, count);
	if (ret != BCME_OK) {
		return ret;
	}
	dhdpcie_bus_membytes(bus, TRUE, addr, rand_buf, count);

#ifdef DHD_RND_DEBUG
	/* Dump random content to out file */
	dhd_dump_rnd_info(bus->dhd, rand_buf, count);
#endif /* DHD_RND_DEBUG */

	return BCME_OK;
}

void
dhd_pcie_intr_count_dump(dhd_pub_t *dhd)
{
	struct dhd_bus *bus = dhd->bus;
	uint64 current_time;

	DHD_ERROR(("\n ------- DUMPING INTR enable/disable counters  ------- \r\n"));
	DHD_ERROR(("resume_intr_enable_count=%lu dpc_intr_enable_count=%lu\n",
		bus->resume_intr_enable_count, bus->dpc_intr_enable_count));
	DHD_ERROR(("isr_intr_disable_count=%lu suspend_intr_disable_count=%lu\n",
		bus->isr_intr_disable_count, bus->suspend_intr_disable_count));
#ifdef BCMPCIE_OOB_HOST_WAKE
	DHD_ERROR(("oob_intr_count=%lu oob_intr_enable_count=%lu oob_intr_disable_count=%lu\n",
		bus->oob_intr_count, bus->oob_intr_enable_count,
		bus->oob_intr_disable_count));
	DHD_ERROR(("oob_irq_num=%d last_oob_irq_time="SEC_USEC_FMT"\n",
		dhdpcie_get_oob_irq_num(bus),
		GET_SEC_USEC(bus->last_oob_irq_time)));
	DHD_ERROR(("last_oob_irq_enable_time="SEC_USEC_FMT
		" last_oob_irq_disable_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(bus->last_oob_irq_enable_time),
		GET_SEC_USEC(bus->last_oob_irq_disable_time)));
	DHD_ERROR(("oob_irq_enabled=%d oob_gpio_level=%d\n",
		dhdpcie_get_oob_irq_status(bus),
		dhdpcie_get_oob_irq_level()));
#endif /* BCMPCIE_OOB_HOST_WAKE */
	DHD_ERROR(("dpc_return_busdown_count=%lu non_ours_irq_count=%lu\n",
		bus->dpc_return_busdown_count, bus->non_ours_irq_count));

	current_time = OSL_LOCALTIME_NS();
	DHD_ERROR(("\ncurrent_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(current_time)));
	DHD_ERROR(("isr_entry_time="SEC_USEC_FMT
		" isr_exit_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(bus->isr_entry_time),
		GET_SEC_USEC(bus->isr_exit_time)));
	DHD_ERROR(("dpc_sched_time="SEC_USEC_FMT
		" last_non_ours_irq_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(bus->dpc_sched_time),
		GET_SEC_USEC(bus->last_non_ours_irq_time)));
	DHD_ERROR(("dpc_entry_time="SEC_USEC_FMT
		" last_process_ctrlbuf_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(bus->dpc_entry_time),
		GET_SEC_USEC(bus->last_process_ctrlbuf_time)));
	DHD_ERROR(("last_process_flowring_time="SEC_USEC_FMT
		" last_process_txcpl_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(bus->last_process_flowring_time),
		GET_SEC_USEC(bus->last_process_txcpl_time)));
	DHD_ERROR(("last_process_rxcpl_time="SEC_USEC_FMT
		" last_process_infocpl_time="SEC_USEC_FMT
		" last_process_edl_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(bus->last_process_rxcpl_time),
		GET_SEC_USEC(bus->last_process_infocpl_time),
		GET_SEC_USEC(bus->last_process_edl_time)));
	DHD_ERROR(("dpc_exit_time="SEC_USEC_FMT
		" resched_dpc_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(bus->dpc_exit_time),
		GET_SEC_USEC(bus->resched_dpc_time)));
	DHD_ERROR(("last_d3_inform_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(bus->last_d3_inform_time)));

	DHD_ERROR(("\nlast_suspend_start_time="SEC_USEC_FMT
		" last_suspend_end_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(bus->last_suspend_start_time),
		GET_SEC_USEC(bus->last_suspend_end_time)));
	DHD_ERROR(("last_resume_start_time="SEC_USEC_FMT
		" last_resume_end_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(bus->last_resume_start_time),
		GET_SEC_USEC(bus->last_resume_end_time)));

#if defined(SHOW_LOGTRACE) && defined(DHD_USE_KTHREAD_FOR_LOGTRACE)
	DHD_ERROR(("logtrace_thread_entry_time="SEC_USEC_FMT
		" logtrace_thread_sem_down_time="SEC_USEC_FMT
		"\nlogtrace_thread_flush_time="SEC_USEC_FMT
		" logtrace_thread_unexpected_break_time="SEC_USEC_FMT
		"\nlogtrace_thread_complete_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(dhd->logtrace_thr_ts.entry_time),
		GET_SEC_USEC(dhd->logtrace_thr_ts.sem_down_time),
		GET_SEC_USEC(dhd->logtrace_thr_ts.flush_time),
		GET_SEC_USEC(dhd->logtrace_thr_ts.unexpected_break_time),
		GET_SEC_USEC(dhd->logtrace_thr_ts.complete_time)));
#endif /* SHOW_LOGTRACE && DHD_USE_KTHREAD_FOR_LOGTRACE */
}

void
dhd_bus_intr_count_dump(dhd_pub_t *dhd)
{
	dhd_pcie_intr_count_dump(dhd);
}

int
dhd_pcie_dump_wrapper_regs(dhd_pub_t *dhd)
{
	uint32 save_idx, val;
	si_t *sih = dhd->bus->sih;
	uint32 oob_base, oob_base1;
	uint32 wrapper_dump_list[] = {
		AI_OOBSELOUTA30, AI_OOBSELOUTA74, AI_OOBSELOUTB30, AI_OOBSELOUTB74,
		AI_OOBSELOUTC30, AI_OOBSELOUTC74, AI_OOBSELOUTD30, AI_OOBSELOUTD74,
		AI_RESETSTATUS, AI_RESETCTRL,
		AI_ITIPOOBA, AI_ITIPOOBB, AI_ITIPOOBC, AI_ITIPOOBD,
		AI_ITIPOOBAOUT, AI_ITIPOOBBOUT, AI_ITIPOOBCOUT, AI_ITIPOOBDOUT
	};
	uint8 i;
	hndoobr_reg_t *reg;
	cr4regs_t *cr4regs;
	ca7regs_t *ca7regs;

	save_idx = si_coreidx(sih);

	DHD_ERROR(("%s: Master wrapper Reg\n", __FUNCTION__));

	if (si_setcore(sih, PCIE2_CORE_ID, 0) != NULL) {
		for (i = 0; i < sizeof(wrapper_dump_list) / 4; i++) {
			val = si_wrapperreg(sih, wrapper_dump_list[i], 0, 0);
			DHD_ERROR(("sbreg: addr:0x%x val:0x%x\n", wrapper_dump_list[i], val));
		}
	}

	if ((cr4regs = si_setcore(sih, ARMCR4_CORE_ID, 0)) != NULL) {
		DHD_ERROR(("%s: ARM CR4 wrapper Reg\n", __FUNCTION__));
		for (i = 0; i < sizeof(wrapper_dump_list) / 4; i++) {
			val = si_wrapperreg(sih, wrapper_dump_list[i], 0, 0);
			DHD_ERROR(("sbreg: addr:0x%x val:0x%x\n", wrapper_dump_list[i], val));
		}
		DHD_ERROR(("%s: ARM CR4 core Reg\n", __FUNCTION__));
		val = R_REG(dhd->osh, ARM_CR4_REG(cr4regs, corecontrol));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(cr4regs_t, corecontrol), val));
		val = R_REG(dhd->osh, ARM_CR4_REG(cr4regs, corecapabilities));
		DHD_ERROR(("reg:0x%x val:0x%x\n",
			(uint)OFFSETOF(cr4regs_t, corecapabilities), val));
		val = R_REG(dhd->osh, ARM_CR4_REG(cr4regs, corestatus));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(cr4regs_t, corestatus), val));
		val = R_REG(dhd->osh, ARM_CR4_REG(cr4regs, nmiisrst));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(cr4regs_t, nmiisrst), val));
		val = R_REG(dhd->osh, ARM_CR4_REG(cr4regs, nmimask));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(cr4regs_t, nmimask), val));
		val = R_REG(dhd->osh, ARM_CR4_REG(cr4regs, isrmask));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(cr4regs_t, isrmask), val));
		val = R_REG(dhd->osh, ARM_CR4_REG(cr4regs, swintreg));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(cr4regs_t, swintreg), val));
		val = R_REG(dhd->osh, ARM_CR4_REG(cr4regs, intstatus));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(cr4regs_t, intstatus), val));
		val = R_REG(dhd->osh, ARM_CR4_REG(cr4regs, cyclecnt));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(cr4regs_t, cyclecnt), val));
		val = R_REG(dhd->osh, ARM_CR4_REG(cr4regs, inttimer));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(cr4regs_t, inttimer), val));
		val = R_REG(dhd->osh, ARM_CR4_REG(cr4regs, clk_ctl_st));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(cr4regs_t, clk_ctl_st), val));
		val = R_REG(dhd->osh, ARM_CR4_REG(cr4regs, powerctl));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(cr4regs_t, powerctl), val));
	}

	if ((ca7regs = si_setcore(sih, ARMCA7_CORE_ID, 0)) != NULL) {
		DHD_ERROR(("%s: ARM CA7 core Reg\n", __FUNCTION__));
		val = R_REG(dhd->osh, ARM_CA7_REG(ca7regs, corecontrol));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(ca7regs_t, corecontrol), val));
		val = R_REG(dhd->osh, ARM_CA7_REG(ca7regs, corecapabilities));
		DHD_ERROR(("reg:0x%x val:0x%x\n",
			(uint)OFFSETOF(ca7regs_t, corecapabilities), val));
		val = R_REG(dhd->osh, ARM_CA7_REG(ca7regs, corestatus));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(ca7regs_t, corestatus), val));
		val = R_REG(dhd->osh, ARM_CA7_REG(ca7regs, tracecontrol));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(ca7regs_t, tracecontrol), val));
		val = R_REG(dhd->osh, ARM_CA7_REG(ca7regs, clk_ctl_st));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(ca7regs_t, clk_ctl_st), val));
		val = R_REG(dhd->osh, ARM_CA7_REG(ca7regs, powerctl));
		DHD_ERROR(("reg:0x%x val:0x%x\n", (uint)OFFSETOF(ca7regs_t, powerctl), val));
	}

	DHD_ERROR(("%s: OOBR Reg\n", __FUNCTION__));

	oob_base = si_oobr_baseaddr(sih, FALSE);
	oob_base1 = si_oobr_baseaddr(sih, TRUE);
	if (oob_base) {
		dhd_sbreg_op(dhd, oob_base + OOB_STATUSA, &val, TRUE);
		dhd_sbreg_op(dhd, oob_base + OOB_STATUSB, &val, TRUE);
		dhd_sbreg_op(dhd, oob_base + OOB_STATUSC, &val, TRUE);
		dhd_sbreg_op(dhd, oob_base + OOB_STATUSD, &val, TRUE);
	} else if ((reg = si_setcore(sih, HND_OOBR_CORE_ID, 0)) != NULL) {
		val = R_REG(dhd->osh, &reg->intstatus[0]);
		DHD_ERROR(("reg: addr:%p val:0x%x\n", reg, val));
		val = R_REG(dhd->osh, &reg->intstatus[1]);
		DHD_ERROR(("reg: addr:%p val:0x%x\n", reg, val));
		val = R_REG(dhd->osh, &reg->intstatus[2]);
		DHD_ERROR(("reg: addr:%p val:0x%x\n", reg, val));
		val = R_REG(dhd->osh, &reg->intstatus[3]);
		DHD_ERROR(("reg: addr:%p val:0x%x\n", reg, val));
	}

	if (oob_base1) {
		DHD_ERROR(("%s: Second OOBR Reg\n", __FUNCTION__));

		dhd_sbreg_op(dhd, oob_base1 + OOB_STATUSA, &val, TRUE);
		dhd_sbreg_op(dhd, oob_base1 + OOB_STATUSB, &val, TRUE);
		dhd_sbreg_op(dhd, oob_base1 + OOB_STATUSC, &val, TRUE);
		dhd_sbreg_op(dhd, oob_base1 + OOB_STATUSD, &val, TRUE);
	}

	si_setcoreidx(dhd->bus->sih, save_idx);

	return 0;
}

int
dhd_pcie_dma_info_dump(dhd_pub_t *dhd)
{
	if (dhd->bus->is_linkdown) {
		DHD_ERROR(("\n ------- SKIP DUMPING DMA Registers "
			"due to PCIe link down ------- \r\n"));
		return 0;
	}

	DHD_ERROR(("\n ------- DUMPING DMA Registers ------- \r\n"));

	//HostToDev
	DHD_ERROR(("HostToDev TX: XmtCtrl=0x%08x XmtPtr=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x200, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x204, 0, 0)));
	DHD_ERROR(("            : XmtAddrLow=0x%08x XmtAddrHigh=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x208, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x20C, 0, 0)));
	DHD_ERROR(("            : XmtStatus0=0x%08x XmtStatus1=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x210, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x214, 0, 0)));

	DHD_ERROR(("HostToDev RX: RcvCtrl=0x%08x RcvPtr=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x220, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x224, 0, 0)));
	DHD_ERROR(("            : RcvAddrLow=0x%08x RcvAddrHigh=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x228, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x22C, 0, 0)));
	DHD_ERROR(("            : RcvStatus0=0x%08x RcvStatus1=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x230, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x234, 0, 0)));

	//DevToHost
	DHD_ERROR(("DevToHost TX: XmtCtrl=0x%08x XmtPtr=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x240, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x244, 0, 0)));
	DHD_ERROR(("            : XmtAddrLow=0x%08x XmtAddrHigh=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x248, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x24C, 0, 0)));
	DHD_ERROR(("            : XmtStatus0=0x%08x XmtStatus1=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x250, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x254, 0, 0)));

	DHD_ERROR(("DevToHost RX: RcvCtrl=0x%08x RcvPtr=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x260, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x264, 0, 0)));
	DHD_ERROR(("            : RcvAddrLow=0x%08x RcvAddrHigh=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x268, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x26C, 0, 0)));
	DHD_ERROR(("            : RcvStatus0=0x%08x RcvStatus1=0x%08x\n",
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x270, 0, 0),
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, 0x274, 0, 0)));

	return 0;
}

bool
dhd_pcie_dump_int_regs(dhd_pub_t *dhd)
{
	uint32 intstatus = 0;
	uint32 intmask = 0;
	uint32 d2h_db0 = 0;
	uint32 d2h_mb_data = 0;

	DHD_ERROR(("\n ------- DUMPING INTR Status and Masks ------- \r\n"));
	intstatus = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
		dhd->bus->pcie_mailbox_int, 0, 0);
	if (intstatus == (uint32)-1) {
		DHD_ERROR(("intstatus=0x%x \n", intstatus));
		return FALSE;
	}

	intmask = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
		dhd->bus->pcie_mailbox_mask, 0, 0);
	if (intmask == (uint32) -1) {
		DHD_ERROR(("intstatus=0x%x intmask=0x%x \n", intstatus, intmask));
		return FALSE;
	}

	d2h_db0 = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
		PCID2H_MailBox, 0, 0);
	if (d2h_db0 == (uint32)-1) {
		DHD_ERROR(("intstatus=0x%x intmask=0x%x d2h_db0=0x%x\n",
		intstatus, intmask, d2h_db0));
		return FALSE;
	}

	DHD_ERROR(("intstatus=0x%x intmask=0x%x d2h_db0=0x%x\n",
		intstatus, intmask, d2h_db0));
	dhd_bus_cmn_readshared(dhd->bus, &d2h_mb_data, D2H_MB_DATA, 0);
	DHD_ERROR(("d2h_mb_data=0x%x def_intmask=0x%x \r\n", d2h_mb_data,
		dhd->bus->def_intmask));

	return TRUE;
}

void
dhd_pcie_dump_rc_conf_space_cap(dhd_pub_t *dhd)
{
	DHD_ERROR(("\n ------- DUMPING PCIE RC config space Registers ------- \r\n"));
	DHD_ERROR(("Pcie RC Uncorrectable Error Status Val=0x%x\n",
		dhdpcie_rc_access_cap(dhd->bus, PCIE_EXTCAP_ID_ERR,
		PCIE_EXTCAP_AER_UCERR_OFFSET, TRUE, FALSE, 0)));
#ifdef EXTENDED_PCIE_DEBUG_DUMP
	DHD_ERROR(("hdrlog0 =0x%08x hdrlog1 =0x%08x hdrlog2 =0x%08x hdrlog3 =0x%08x\n",
		dhdpcie_rc_access_cap(dhd->bus, PCIE_EXTCAP_ID_ERR,
		PCIE_EXTCAP_ERR_HEADER_LOG_0, TRUE, FALSE, 0),
		dhdpcie_rc_access_cap(dhd->bus, PCIE_EXTCAP_ID_ERR,
		PCIE_EXTCAP_ERR_HEADER_LOG_1, TRUE, FALSE, 0),
		dhdpcie_rc_access_cap(dhd->bus, PCIE_EXTCAP_ID_ERR,
		PCIE_EXTCAP_ERR_HEADER_LOG_2, TRUE, FALSE, 0),
		dhdpcie_rc_access_cap(dhd->bus, PCIE_EXTCAP_ID_ERR,
		PCIE_EXTCAP_ERR_HEADER_LOG_3, TRUE, FALSE, 0)));
#endif /* EXTENDED_PCIE_DEBUG_DUMP */
}

int
dhd_pcie_debug_info_dump(dhd_pub_t *dhd)
{
	int host_irq_disabled;

	DHD_ERROR(("bus->bus_low_power_state = %d\n", dhd->bus->bus_low_power_state));
	host_irq_disabled = dhdpcie_irq_disabled(dhd->bus);
	DHD_ERROR(("host pcie_irq disabled = %d\n", host_irq_disabled));
	dhd_print_tasklet_status(dhd);
	dhd_pcie_intr_count_dump(dhd);

	DHD_ERROR(("\n ------- DUMPING PCIE EP Resouce Info ------- \r\n"));
	dhdpcie_dump_resource(dhd->bus);

	dhd_pcie_dump_rc_conf_space_cap(dhd);

	DHD_ERROR(("RootPort PCIe linkcap=0x%08x\n",
		dhd_debug_get_rc_linkcap(dhd->bus)));

	if (dhd->bus->is_linkdown && !dhd->bus->cto_triggered) {
		DHD_ERROR(("Skip dumping the PCIe Config and Core registers. "
			"link may be DOWN\n"));
		return 0;
	}

	DHD_ERROR(("\n ------- DUMPING PCIE EP config space Registers ------- \r\n"));
	DHD_ERROR(("Status Command(0x%x)=0x%x, BaseAddress0(0x%x)=0x%x BaseAddress1(0x%x)=0x%x "
		"PCIE_CFG_PMCSR(0x%x)=0x%x\n",
		PCIECFGREG_STATUS_CMD,
		dhd_pcie_config_read(dhd->bus->osh, PCIECFGREG_STATUS_CMD, sizeof(uint32)),
		PCIECFGREG_BASEADDR0,
		dhd_pcie_config_read(dhd->bus->osh, PCIECFGREG_BASEADDR0, sizeof(uint32)),
		PCIECFGREG_BASEADDR1,
		dhd_pcie_config_read(dhd->bus->osh, PCIECFGREG_BASEADDR1, sizeof(uint32)),
		PCIE_CFG_PMCSR,
		dhd_pcie_config_read(dhd->bus->osh, PCIE_CFG_PMCSR, sizeof(uint32))));
	DHD_ERROR(("LinkCtl(0x%x)=0x%x DeviceStatusControl2(0x%x)=0x%x "
		"L1SSControl(0x%x)=0x%x\n", PCIECFGREG_LINK_STATUS_CTRL,
		dhd_pcie_config_read(dhd->bus->osh, PCIECFGREG_LINK_STATUS_CTRL,
		sizeof(uint32)), PCIECFGGEN_DEV_STATUS_CTRL2,
		dhd_pcie_config_read(dhd->bus->osh, PCIECFGGEN_DEV_STATUS_CTRL2,
		sizeof(uint32)), PCIECFGREG_PML1_SUB_CTRL1,
		dhd_pcie_config_read(dhd->bus->osh, PCIECFGREG_PML1_SUB_CTRL1,
		sizeof(uint32))));
#ifdef EXTENDED_PCIE_DEBUG_DUMP
	DHD_ERROR(("Pcie EP Uncorrectable Error Status Val=0x%x\n",
		dhdpcie_ep_access_cap(dhd->bus, PCIE_EXTCAP_ID_ERR,
		PCIE_EXTCAP_AER_UCERR_OFFSET, TRUE, FALSE, 0)));
	DHD_ERROR(("hdrlog0(0x%x)=0x%08x hdrlog1(0x%x)=0x%08x hdrlog2(0x%x)=0x%08x "
		"hdrlog3(0x%x)=0x%08x\n", PCI_TLP_HDR_LOG1,
		dhd_pcie_config_read(dhd->bus->osh, PCI_TLP_HDR_LOG1, sizeof(uint32)),
		PCI_TLP_HDR_LOG2,
		dhd_pcie_config_read(dhd->bus->osh, PCI_TLP_HDR_LOG2, sizeof(uint32)),
		PCI_TLP_HDR_LOG3,
		dhd_pcie_config_read(dhd->bus->osh, PCI_TLP_HDR_LOG3, sizeof(uint32)),
		PCI_TLP_HDR_LOG4,
		dhd_pcie_config_read(dhd->bus->osh, PCI_TLP_HDR_LOG4, sizeof(uint32))));
	if (dhd->bus->sih->buscorerev >= 24) {
		DHD_ERROR(("DeviceStatusControl(0x%x)=0x%x SubsystemControl(0x%x)=0x%x "
			"L1SSControl2(0x%x)=0x%x\n", PCIECFGREG_DEV_STATUS_CTRL,
			dhd_pcie_config_read(dhd->bus->osh, PCIECFGREG_DEV_STATUS_CTRL,
			sizeof(uint32)), PCIE_CFG_SUBSYSTEM_CONTROL,
			dhd_pcie_config_read(dhd->bus->osh, PCIE_CFG_SUBSYSTEM_CONTROL,
			sizeof(uint32)), PCIECFGREG_PML1_SUB_CTRL2,
			dhd_pcie_config_read(dhd->bus->osh, PCIECFGREG_PML1_SUB_CTRL2,
			sizeof(uint32))));
		dhd_bus_dump_dar_registers(dhd->bus);
	}
#endif /* EXTENDED_PCIE_DEBUG_DUMP */

	if (dhd->bus->is_linkdown) {
		DHD_ERROR(("Skip dumping the PCIe Core registers. link may be DOWN\n"));
		return 0;
	}

	DHD_ERROR(("\n ------- DUMPING PCIE core Registers ------- \r\n"));

	DHD_ERROR(("ClkReq0(0x%x)=0x%x ClkReq1(0x%x)=0x%x ClkReq2(0x%x)=0x%x "
		"ClkReq3(0x%x)=0x%x\n", PCIECFGREG_PHY_DBG_CLKREQ0,
		dhd_pcie_corereg_read(dhd->bus->sih, PCIECFGREG_PHY_DBG_CLKREQ0),
		PCIECFGREG_PHY_DBG_CLKREQ1,
		dhd_pcie_corereg_read(dhd->bus->sih, PCIECFGREG_PHY_DBG_CLKREQ1),
		PCIECFGREG_PHY_DBG_CLKREQ2,
		dhd_pcie_corereg_read(dhd->bus->sih, PCIECFGREG_PHY_DBG_CLKREQ2),
		PCIECFGREG_PHY_DBG_CLKREQ3,
		dhd_pcie_corereg_read(dhd->bus->sih, PCIECFGREG_PHY_DBG_CLKREQ3)));

#ifdef EXTENDED_PCIE_DEBUG_DUMP
	if (dhd->bus->sih->buscorerev >= 24) {

		DHD_ERROR(("ltssm_hist_0(0x%x)=0x%x ltssm_hist_1(0x%x)=0x%x "
			"ltssm_hist_2(0x%x)=0x%x "
			"ltssm_hist_3(0x%x)=0x%x\n", PCIECFGREG_PHY_LTSSM_HIST_0,
			dhd_pcie_corereg_read(dhd->bus->sih, PCIECFGREG_PHY_LTSSM_HIST_0),
			PCIECFGREG_PHY_LTSSM_HIST_1,
			dhd_pcie_corereg_read(dhd->bus->sih, PCIECFGREG_PHY_LTSSM_HIST_1),
			PCIECFGREG_PHY_LTSSM_HIST_2,
			dhd_pcie_corereg_read(dhd->bus->sih, PCIECFGREG_PHY_LTSSM_HIST_2),
			PCIECFGREG_PHY_LTSSM_HIST_3,
			dhd_pcie_corereg_read(dhd->bus->sih, PCIECFGREG_PHY_LTSSM_HIST_3)));

		DHD_ERROR(("trefup(0x%x)=0x%x trefup_ext(0x%x)=0x%x\n",
			PCIECFGREG_TREFUP,
			dhd_pcie_corereg_read(dhd->bus->sih, PCIECFGREG_TREFUP),
			PCIECFGREG_TREFUP_EXT,
			dhd_pcie_corereg_read(dhd->bus->sih, PCIECFGREG_TREFUP_EXT)));
		DHD_ERROR(("errlog(0x%x)=0x%x errlog_addr(0x%x)=0x%x "
			"Function_Intstatus(0x%x)=0x%x "
			"Function_Intmask(0x%x)=0x%x Power_Intstatus(0x%x)=0x%x "
			"Power_Intmask(0x%x)=0x%x\n",
			PCIE_CORE_REG_ERRLOG,
			si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			PCIE_CORE_REG_ERRLOG, 0, 0),
			PCIE_CORE_REG_ERR_ADDR,
			si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
				PCIE_CORE_REG_ERR_ADDR, 0, 0),
			PCIFunctionIntstatus(dhd->bus->sih->buscorerev),
			si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
				PCIFunctionIntstatus(dhd->bus->sih->buscorerev), 0, 0),
			PCIFunctionIntmask(dhd->bus->sih->buscorerev),
			si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
				PCIFunctionIntmask(dhd->bus->sih->buscorerev), 0, 0),
			PCIPowerIntstatus(dhd->bus->sih->buscorerev),
			si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
				PCIPowerIntstatus(dhd->bus->sih->buscorerev), 0, 0),
			PCIPowerIntmask(dhd->bus->sih->buscorerev),
			si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
				PCIPowerIntmask(dhd->bus->sih->buscorerev), 0, 0)));
		DHD_ERROR(("err_hdrlog1(0x%x)=0x%x err_hdrlog2(0x%x)=0x%x "
			"err_hdrlog3(0x%x)=0x%x err_hdrlog4(0x%x)=0x%x\n",
			(uint)OFFSETOF(sbpcieregs_t, u.pcie2.err_hdr_logreg1),
			si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
				OFFSETOF(sbpcieregs_t, u.pcie2.err_hdr_logreg1), 0, 0),
			(uint)OFFSETOF(sbpcieregs_t, u.pcie2.err_hdr_logreg2),
			si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
				OFFSETOF(sbpcieregs_t, u.pcie2.err_hdr_logreg2), 0, 0),
			(uint)OFFSETOF(sbpcieregs_t, u.pcie2.err_hdr_logreg3),
			si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
				OFFSETOF(sbpcieregs_t, u.pcie2.err_hdr_logreg3), 0, 0),
			(uint)OFFSETOF(sbpcieregs_t, u.pcie2.err_hdr_logreg4),
			si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
				OFFSETOF(sbpcieregs_t, u.pcie2.err_hdr_logreg4), 0, 0)));
		DHD_ERROR(("err_code(0x%x)=0x%x\n",
			(uint)OFFSETOF(sbpcieregs_t, u.pcie2.err_code_logreg),
			si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
				OFFSETOF(sbpcieregs_t, u.pcie2.err_code_logreg), 0, 0)));

		dhd_pcie_dump_wrapper_regs(dhd);
	}
#endif /* EXTENDED_PCIE_DEBUG_DUMP */

	dhd_pcie_dma_info_dump(dhd);

	return 0;
}

bool
dhd_bus_force_bt_quiesce_enabled(struct dhd_bus *bus)
{
	return bus->force_bt_quiesce;
}

#ifdef DHD_HP2P
uint16
dhd_bus_get_hp2p_ring_max_size(struct dhd_bus *bus, bool tx)
{
	if (tx)
		return bus->hp2p_txcpl_max_items;
	else
		return bus->hp2p_rxcpl_max_items;
}

static uint16
dhd_bus_set_hp2p_ring_max_size(struct dhd_bus *bus, bool tx, uint16 val)
{
	if (tx)
		bus->hp2p_txcpl_max_items = val;
	else
		bus->hp2p_rxcpl_max_items = val;
	return val;
}
#endif /* DHD_HP2P */

static bool
dhd_bus_tcm_test(struct dhd_bus *bus)
{
	int ret = 0;
	int size; /* Full mem size */
	int start; /* Start address */
	int read_size = 0; /* Read size of each iteration */
	int num = 0;
	uint8 *read_buf, *write_buf;
	uint8 init_val[NUM_PATTERNS] = {
		0xFFu, /* 11111111 */
		0x00u, /* 00000000 */
	};

	if (!bus) {
		DHD_ERROR(("%s: bus is NULL !\n", __FUNCTION__));
		return FALSE;
	}

	read_buf = MALLOCZ(bus->dhd->osh, MEMBLOCK);

	if (!read_buf) {
		DHD_ERROR(("%s: MALLOC of read_buf failed\n", __FUNCTION__));
		return FALSE;
	}

	write_buf = MALLOCZ(bus->dhd->osh, MEMBLOCK);

	if (!write_buf) {
		MFREE(bus->dhd->osh, read_buf, MEMBLOCK);
		DHD_ERROR(("%s: MALLOC of write_buf failed\n", __FUNCTION__));
		return FALSE;
	}

	DHD_ERROR(("%s: start %x,  size: %x\n", __FUNCTION__, bus->dongle_ram_base, bus->ramsize));
	DHD_ERROR(("%s: memblock size %d,  #pattern %d\n", __FUNCTION__, MEMBLOCK, NUM_PATTERNS));

	while (num < NUM_PATTERNS) {
		start = bus->dongle_ram_base;
		/* Get full mem size */
		size = bus->ramsize;

		memset(write_buf, init_val[num], MEMBLOCK);
		while (size > 0) {
			read_size = MIN(MEMBLOCK, size);
			memset(read_buf, 0, read_size);

			/* Write */
			if ((ret = dhdpcie_bus_membytes(bus, TRUE, start, write_buf, read_size))) {
				DHD_ERROR(("%s: Write Error membytes %d\n", __FUNCTION__, ret));
				MFREE(bus->dhd->osh, read_buf, MEMBLOCK);
				MFREE(bus->dhd->osh, write_buf, MEMBLOCK);
				return FALSE;
			}

			/* Read */
			if ((ret = dhdpcie_bus_membytes(bus, FALSE, start, read_buf, read_size))) {
				DHD_ERROR(("%s: Read Error membytes %d\n", __FUNCTION__, ret));
				MFREE(bus->dhd->osh, read_buf, MEMBLOCK);
				MFREE(bus->dhd->osh, write_buf, MEMBLOCK);
				return FALSE;
			}

			/* Compare */
			if (memcmp(read_buf, write_buf, read_size)) {
				DHD_ERROR(("%s: Mismatch at %x, iter : %d\n",
					__FUNCTION__, start, num));
				prhex("Readbuf", read_buf, read_size);
				prhex("Writebuf", write_buf, read_size);
				MFREE(bus->dhd->osh, read_buf, MEMBLOCK);
				MFREE(bus->dhd->osh, write_buf, MEMBLOCK);
				return FALSE;
			}

			/* Decrement size and increment start address */
			size -= read_size;
			start += read_size;
		}
		num++;
	}

	MFREE(bus->dhd->osh, read_buf, MEMBLOCK);
	MFREE(bus->dhd->osh, write_buf, MEMBLOCK);

	DHD_ERROR(("%s: Success iter : %d\n", __FUNCTION__, num));
	return TRUE;
}
