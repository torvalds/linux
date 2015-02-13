/*
 * DHD Bus Module for PCIE
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: dhd_pcie.c 491657 2014-07-17 06:29:40Z $
 */


/* include files */
#include <typedefs.h>
#include <bcmutils.h>
#include <bcmdevs.h>
#include <siutils.h>
#include <hndsoc.h>
#include <hndpmu.h>
#include <sbchipc.h>
#if defined(DHD_DEBUG)
#include <hnd_armtrap.h>
#include <hnd_cons.h>
#endif /* defined(DHD_DEBUG) */
#include <dngl_stats.h>
#include <pcie_core.h>
#include <dhd.h>
#include <dhd_bus.h>
#include <dhd_flowring.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <dhdioctl.h>
#include <sdiovar.h>
#include <bcmmsgbuf.h>
#include <pcicfg.h>
#include <dhd_pcie.h>
#include <bcmpcie.h>
#include <bcmendian.h>
#ifdef DHDTCPACK_SUPPRESS
#include <dhd_ip.h>
#endif /* DHDTCPACK_SUPPRESS */

#ifdef BCMEMBEDIMAGE
#include BCMEMBEDIMAGE
#endif /* BCMEMBEDIMAGE */

#define MEMBLOCK	2048		/* Block size used for downloading of dongle image */
#define MAX_NVRAMBUF_SIZE	6144	/* max nvram buf size */

#define ARMCR4REG_BANKIDX	(0x40/sizeof(uint32))
#define ARMCR4REG_BANKPDA	(0x4C/sizeof(uint32))
/* Temporary war to fix precommit till sync issue between trunk & precommit branch is resolved */

int dhd_dongle_memsize;
int dhd_dongle_ramsize;
#ifdef DHD_DEBUG
static int dhdpcie_checkdied(dhd_bus_t *bus, char *data, uint size);
static int dhdpcie_bus_readconsole(dhd_bus_t *bus);
#endif
static int dhdpcie_bus_membytes(dhd_bus_t *bus, bool write, ulong address, uint8 *data, uint size);
static int dhdpcie_bus_doiovar(dhd_bus_t *bus, const bcm_iovar_t *vi, uint32 actionid,
	const char *name, void *params,
	int plen, void *arg, int len, int val_size);
static int dhdpcie_bus_lpback_req(struct  dhd_bus *bus, uint32 intval);
static int dhdpcie_bus_dmaxfer_req(struct  dhd_bus *bus,
	uint32 len, uint32 srcdelay, uint32 destdelay);
static int dhdpcie_bus_download_state(dhd_bus_t *bus, bool enter);
static int _dhdpcie_download_firmware(struct dhd_bus *bus);
static int dhdpcie_download_firmware(dhd_bus_t *bus, osl_t *osh);
static int dhdpcie_bus_write_vars(dhd_bus_t *bus);
static void dhdpcie_bus_process_mailbox_intr(dhd_bus_t *bus, uint32 intstatus);
static void dhdpci_bus_read_frames(dhd_bus_t *bus);
static int dhdpcie_readshared(dhd_bus_t *bus);
static void dhdpcie_init_shared_addr(dhd_bus_t *bus);
static bool dhdpcie_dongle_attach(dhd_bus_t *bus);
static void dhdpcie_bus_intr_enable(dhd_bus_t *bus);
static void dhdpcie_bus_dongle_setmemsize(dhd_bus_t *bus, int mem_size);
static void dhdpcie_bus_release_dongle(dhd_bus_t *bus, osl_t *osh,
	bool dongle_isolation, bool reset_flag);
static void dhdpcie_bus_release_malloc(dhd_bus_t *bus, osl_t *osh);
static int dhdpcie_downloadvars(dhd_bus_t *bus, void *arg, int len);
static uint8 dhdpcie_bus_rtcm8(dhd_bus_t *bus, ulong offset);
static void dhdpcie_bus_wtcm8(dhd_bus_t *bus, ulong offset, uint8 data);
static void dhdpcie_bus_wtcm16(dhd_bus_t *bus, ulong offset, uint16 data);
static uint16 dhdpcie_bus_rtcm16(dhd_bus_t *bus, ulong offset);
static void dhdpcie_bus_wtcm32(dhd_bus_t *bus, ulong offset, uint32 data);
static uint32 dhdpcie_bus_rtcm32(dhd_bus_t *bus, ulong offset);
static void dhdpcie_bus_wtcm64(dhd_bus_t *bus, ulong offset, uint64 data);
static uint64 dhdpcie_bus_rtcm64(dhd_bus_t *bus, ulong offset);
static void dhdpcie_bus_cfg_set_bar0_win(dhd_bus_t *bus, uint32 data);
static void dhdpcie_bus_reg_unmap(osl_t *osh, ulong addr, int size);
static int dhdpcie_cc_nvmshadow(dhd_bus_t *bus, struct bcmstrbuf *b);
static void dhdpcie_send_mb_data(dhd_bus_t *bus, uint32 h2d_mb_data);
static void dhd_fillup_ring_sharedptr_info(dhd_bus_t *bus, ring_info_t *ring_info);
extern void dhd_dpc_kill(dhd_pub_t *dhdp);

#ifdef BCMEMBEDIMAGE
static int dhdpcie_download_code_array(dhd_bus_t *bus);
#endif /* BCMEMBEDIMAGE */



#define     PCI_VENDOR_ID_BROADCOM          0x14e4

/* IOVar table */
enum {
	IOV_INTR = 1,
	IOV_MEMBYTES,
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
	IOV_PCIEREG,
	IOV_PCIECFGREG,
	IOV_PCIECOREREG,
	IOV_PCIESERDESREG,
	IOV_BAR0_SECWIN_REG,
	IOV_SBREG,
	IOV_DONGLEISOLATION,
	IOV_LTRSLEEPON_UNLOOAD,
	IOV_RX_METADATALEN,
	IOV_TX_METADATALEN,
	IOV_TXP_THRESHOLD,
	IOV_BUZZZ_DUMP,
	IOV_DUMP_RINGUPD_BLOCK,
	IOV_DMA_RINGINDICES,
	IOV_DB1_FOR_MB,
	IOV_FLOW_PRIO_MAP
};


const bcm_iovar_t dhdpcie_iovars[] = {
	{"intr",	IOV_INTR,	0,	IOVT_BOOL,	0 },
	{"membytes",	IOV_MEMBYTES,	0,	IOVT_BUFFER,	2 * sizeof(int) },
	{"memsize",	IOV_MEMSIZE,	0,	IOVT_UINT32,	0 },
	{"dwnldstate",	IOV_SET_DOWNLOAD_STATE,	0,	IOVT_BOOL,	0 },
	{"vars",	IOV_VARS,	0,	IOVT_BUFFER,	0 },
	{"devreset",	IOV_DEVRESET,	0,	IOVT_BOOL,	0 },
	{"pcie_lpbk",	IOV_PCIE_LPBK,	0,	IOVT_UINT32,	0 },
	{"cc_nvmshadow", IOV_CC_NVMSHADOW, 0, IOVT_BUFFER, 0 },
	{"ramsize",	IOV_RAMSIZE,	0,	IOVT_UINT32,	0 },
	{"ramstart",	IOV_RAMSTART,	0,	IOVT_UINT32,	0 },
	{"pciereg",	IOV_PCIEREG,	0,	IOVT_BUFFER,	2 * sizeof(int32) },
	{"pciecfgreg",	IOV_PCIECFGREG,	0,	IOVT_BUFFER,	2 * sizeof(int32) },
	{"pciecorereg",	IOV_PCIECOREREG,	0,	IOVT_BUFFER,	2 * sizeof(int32) },
	{"pcieserdesreg",	IOV_PCIESERDESREG,	0,	IOVT_BUFFER,	3 * sizeof(int32) },
	{"bar0secwinreg",	IOV_BAR0_SECWIN_REG,	0,	IOVT_BUFFER,	2 * sizeof(int32) },
	{"sbreg",	IOV_SBREG,	0,	IOVT_BUFFER,	sizeof(sdreg_t) },
	{"pcie_dmaxfer",	IOV_PCIE_DMAXFER,	0,	IOVT_BUFFER,	3 * sizeof(int32) },
	{"pcie_suspend", IOV_PCIE_SUSPEND,	0,	IOVT_UINT32,	0 },
	{"sleep_allowed",	IOV_SLEEP_ALLOWED,	0,	IOVT_BOOL,	0 },
	{"dngl_isolation", IOV_DONGLEISOLATION,	0,	IOVT_UINT32,	0 },
	{"ltrsleep_on_unload", IOV_LTRSLEEPON_UNLOOAD,	0,	IOVT_UINT32,	0 },
	{"dump_ringupdblk", IOV_DUMP_RINGUPD_BLOCK,	0,	IOVT_BUFFER,	0 },
	{"dma_ring_indices", IOV_DMA_RINGINDICES,	0,	IOVT_UINT32,	0},
	{"rx_metadata_len", IOV_RX_METADATALEN,	0,	IOVT_UINT32,	0 },
	{"tx_metadata_len", IOV_TX_METADATALEN,	0,	IOVT_UINT32,	0 },
	{"db1_for_mb", IOV_DB1_FOR_MB,	0,	IOVT_UINT32,	0 },
	{"txp_thresh", IOV_TXP_THRESHOLD,	0,	IOVT_UINT32,	0 },
	{"buzzz_dump", IOV_BUZZZ_DUMP,		0,	IOVT_UINT32,	0 },
	{"flow_prio_map", IOV_FLOW_PRIO_MAP,	0,	IOVT_UINT32,	0 },
	{NULL, 0, 0, 0, 0 }
};

#define MAX_READ_TIMEOUT	5 * 1000 * 1000

/* Register/Unregister functions are called by the main DHD entry
 * point (e.g. module insertion) to link with the bus driver, in
 * order to look for or await the device.
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
dhdpcie_bus_reg_unmap(osl_t *osh, ulong addr, int size)
{
	REG_UNMAP((void*)(uintptr)addr);
	return;
}

/**
 * 'regs' is the host virtual address that maps to the start of the PCIe BAR0 window. The first 4096
 * bytes in this window are mapped to the backplane address in the PCIEBAR0Window register. The
 * precondition is that the PCIEBAR0Window register 'points' at the PCIe core.
 *
 * 'tcm' is the *host* virtual address at which tcm is mapped.
 */
dhd_bus_t* dhdpcie_bus_attach(osl_t *osh, volatile char* regs, volatile char* tcm)
{
	dhd_bus_t *bus;

	DHD_TRACE(("%s: ENTER\n", __FUNCTION__));

	do {
		if (!(bus = MALLOC(osh, sizeof(dhd_bus_t)))) {
			DHD_ERROR(("%s: MALLOC of dhd_bus_t failed\n", __FUNCTION__));
			break;
		}
		bzero(bus, sizeof(dhd_bus_t));
		bus->regs = regs;
		bus->tcm = tcm;
		bus->osh = osh;

		dll_init(&bus->const_flowring);

		/* Attach pcie shared structure */
		bus->pcie_sh = MALLOC(osh, sizeof(pciedev_shared_t));

		/* dhd_common_init(osh); */

		if (dhdpcie_dongle_attach(bus)) {
			DHD_ERROR(("%s: dhdpcie_probe_attach failed\n", __FUNCTION__));
			break;
		}

		/* software resources */
		if (!(bus->dhd = dhd_attach(osh, bus, PCMSGBUF_HDRLEN))) {
			DHD_ERROR(("%s: dhd_attach failed\n", __FUNCTION__));

			break;
		}
		bus->dhd->busstate = DHD_BUS_DOWN;
		bus->db1_for_mb = TRUE;
		bus->dhd->hang_report  = TRUE;

		DHD_TRACE(("%s: EXIT SUCCESS\n",
			__FUNCTION__));

		return bus;
	} while (0);

	DHD_TRACE(("%s: EXIT FAILURE\n", __FUNCTION__));

	return NULL;
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

/* Get Chip ID version */
uint dhd_bus_chip_id(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = dhdp->bus;
	return  bus->sih->chip;
}

/* Get Chip Rev ID version */
uint dhd_bus_chiprev_id(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = dhdp->bus;
	return bus->sih->chiprev;
}

/* Get Chip Pkg ID version */
uint dhd_bus_chippkg_id(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = dhdp->bus;
	return bus->sih->chippkg;
}


/*

Name:  dhdpcie_bus_isr

Parametrs:

1: IN int irq   -- interrupt vector
2: IN void *arg      -- handle to private data structure

Return value:

Status (TRUE or FALSE)

Description:
Interrupt Service routine checks for the status register,
disable interrupt and queue DPC if mail box interrupts are raised.
*/


int32
dhdpcie_bus_isr(dhd_bus_t *bus)
{

	do {
			DHD_TRACE(("%s: Enter\n", __FUNCTION__));
			/* verify argument */
			if (!bus) {
				DHD_ERROR(("%s : bus is null pointer , exit \n", __FUNCTION__));
				break;
			}

			if (bus->dhd->busstate == DHD_BUS_DOWN) {
				DHD_ERROR(("%s : bus is down. we have nothing to do\n",
					__FUNCTION__));
				break;
			}

			/*  Overall operation:
			 *    - Mask further interrupts
			 *    - Read/ack intstatus
			 *    - Take action based on bits and state
			 *    - Reenable interrupts (as per state)
			 */

			/* Count the interrupt call */
			bus->intrcount++;

			/* read interrupt status register!! Status bits will be cleared in DPC !! */
			bus->ipend = TRUE;
			dhdpcie_bus_intr_disable(bus); /* Disable interrupt!! */
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

static bool
dhdpcie_dongle_attach(dhd_bus_t *bus)
{

	osl_t *osh = bus->osh;
	void *regsva = (void*)bus->regs;
	uint16 devid = bus->cl_devid;
	uint32 val;
	sbpcieregs_t *sbpcieregs;

	DHD_TRACE(("%s: ENTER\n",
		__FUNCTION__));

	bus->alp_only = TRUE;
	bus->sih = NULL;

	/* Set bar0 window to si_enum_base */
	dhdpcie_bus_cfg_set_bar0_win(bus, SI_ENUM_BASE);

	/* si_attach() will provide an SI handle and scan the backplane */
	if (!(bus->sih = si_attach((uint)devid, osh, regsva, PCI_BUS, bus,
	                           &bus->vars, &bus->varsz))) {
		DHD_ERROR(("%s: si_attach failed!\n", __FUNCTION__));
		goto fail;
	}


	si_setcore(bus->sih, PCIE2_CORE_ID, 0);
	sbpcieregs = (sbpcieregs_t*)(bus->regs);

	/* WAR where the BAR1 window may not be sized properly */
	W_REG(osh, &sbpcieregs->configaddr, 0x4e0);
	val = R_REG(osh, &sbpcieregs->configdata);
	W_REG(osh, &sbpcieregs->configdata, val);

	/* Get info on the ARM and SOCRAM cores... */
	/* Should really be qualified by device id */
	if ((si_setcore(bus->sih, ARM7S_CORE_ID, 0)) ||
	    (si_setcore(bus->sih, ARMCM3_CORE_ID, 0)) ||
	    (si_setcore(bus->sih, ARMCR4_CORE_ID, 0))) {
		bus->armrev = si_corerev(bus->sih);
	} else {
		DHD_ERROR(("%s: failed to find ARM core!\n", __FUNCTION__));
		goto fail;
	}

	if (!si_setcore(bus->sih, ARMCR4_CORE_ID, 0)) {
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
		case BCM4356_CHIP_ID:
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
		case BCM4345_CHIP_ID:
			bus->dongle_ram_base = CR4_4345_RAM_BASE;
			break;
		case BCM43602_CHIP_ID:
			bus->dongle_ram_base = CR4_43602_RAM_BASE;
			break;
		case BCM4349_CHIP_GRPID:
			bus->dongle_ram_base = CR4_4349_RAM_BASE;
			break;
		default:
			bus->dongle_ram_base = 0;
			DHD_ERROR(("%s: WARNING: Using default ram base at 0x%x\n",
			           __FUNCTION__, bus->dongle_ram_base));
		}
	}
	bus->ramsize = bus->orig_ramsize;
	if (dhd_dongle_memsize)
		dhdpcie_bus_dongle_setmemsize(bus, dhd_dongle_memsize);

	DHD_ERROR(("DHD: dongle ram size is set to %d(orig %d) at 0x%x\n",
	           bus->ramsize, bus->orig_ramsize, bus->dongle_ram_base));

	bus->srmemsize = si_socram_srmem_size(bus->sih);


	bus->def_intmask = PCIE_MB_D2H_MB_MASK | PCIE_MB_TOPCIE_FN0_0 | PCIE_MB_TOPCIE_FN0_1;

	/* Set the poll and/or interrupt flags */
	bus->intr = (bool)dhd_intr;

	bus->wait_for_d3_ack = 1;
	DHD_TRACE(("%s: EXIT: SUCCESS\n",
		__FUNCTION__));
	return 0;

fail:
	if (bus->sih != NULL)
		si_detach(bus->sih);
	DHD_TRACE(("%s: EXIT: FAILURE\n",
		__FUNCTION__));
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

void
dhdpcie_bus_intr_enable(dhd_bus_t *bus)
{
	DHD_TRACE(("enable interrupts\n"));

	if (!bus || !bus->sih)
		return;

	if ((bus->sih->buscorerev == 2) || (bus->sih->buscorerev == 6) ||
		(bus->sih->buscorerev == 4)) {
		dhpcie_bus_unmask_interrupt(bus);
	}
	else if (bus->sih) {
		si_corereg(bus->sih, bus->sih->buscoreidx, PCIMailBoxMask,
			bus->def_intmask, bus->def_intmask);
	}
}

void
dhdpcie_bus_intr_disable(dhd_bus_t *bus)
{

	DHD_TRACE(("%s Enter\n", __FUNCTION__));

	if (!bus || !bus->sih)
		return;

	if ((bus->sih->buscorerev == 2) || (bus->sih->buscorerev == 6) ||
		(bus->sih->buscorerev == 4)) {
		dhpcie_bus_mask_interrupt(bus);
	}
	else if (bus->sih) {
		si_corereg(bus->sih, bus->sih->buscoreidx, PCIMailBoxMask,
			bus->def_intmask, 0);
	}

	DHD_TRACE(("%s Exit\n", __FUNCTION__));
}


/* Detach and free everything */
void
dhdpcie_bus_release(dhd_bus_t *bus)
{
	bool dongle_isolation = FALSE;
	osl_t *osh = NULL;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus) {

		osh = bus->osh;
		ASSERT(osh);

		if (bus->dhd) {
			dongle_isolation = bus->dhd->dongle_isolation;
			dhd_detach(bus->dhd);

			if (bus->intr) {
				dhdpcie_bus_intr_disable(bus);
				dhdpcie_free_irq(bus);
			}
			dhdpcie_bus_release_dongle(bus, osh, dongle_isolation, TRUE);
			dhd_free(bus->dhd);
			bus->dhd = NULL;
		}

		/* unmap the regs and tcm here!! */
		if (bus->regs) {
			dhdpcie_bus_reg_unmap(osh, (ulong)bus->regs, DONGLE_REG_MAP_SIZE);
			bus->regs = NULL;
		}
		if (bus->tcm) {
			dhdpcie_bus_reg_unmap(osh, (ulong)bus->tcm, DONGLE_TCM_MAP_SIZE);
			bus->tcm = NULL;
		}

		dhdpcie_bus_release_malloc(bus, osh);
		/* Detach pcie shared structure */
		if (bus->pcie_sh)
			MFREE(osh, bus->pcie_sh, sizeof(pciedev_shared_t));

#ifdef DHD_DEBUG

		if (bus->console.buf != NULL)
			MFREE(osh, bus->console.buf, bus->console.bufsize);
#endif


		/* Finally free bus info */
		MFREE(osh, bus, sizeof(dhd_bus_t));

	}

	DHD_TRACE(("%s: Exit\n", __FUNCTION__));

}


void
dhdpcie_bus_release_dongle(dhd_bus_t *bus, osl_t *osh, bool dongle_isolation, bool reset_flag)
{

	DHD_TRACE(("%s Enter\n", __FUNCTION__));

	DHD_TRACE(("%s: Enter bus->dhd %p bus->dhd->dongle_reset %d \n", __FUNCTION__,
		bus->dhd, bus->dhd->dongle_reset));

	if ((bus->dhd && bus->dhd->dongle_reset) && reset_flag) {
		DHD_TRACE(("%s Exit\n", __FUNCTION__));
		return;
	}

	if (bus->sih) {

		if (!dongle_isolation)
			pcie_watchdog_reset(bus->osh, bus->sih, (sbpcieregs_t *)(bus->regs));

		if (bus->ltrsleep_on_unload) {
			si_corereg(bus->sih, bus->sih->buscoreidx,
				OFFSETOF(sbpcieregs_t, u.pcie2.ltr_state), ~0, 0);
		}
		si_detach(bus->sih);
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

/* 32 bit config write */
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

/* Stop bus module: clear pending frames, disable data flow */
void dhd_bus_stop(struct dhd_bus *bus, bool enforce_mutex)
{
	uint32 status;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (!bus->dhd)
		return;

	if (bus->dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s: already down by net_dev_reset\n", __FUNCTION__));
		goto done;
	}

	bus->dhd->busstate = DHD_BUS_DOWN;
	dhdpcie_bus_intr_disable(bus);
	status =  dhdpcie_bus_cfg_read_dword(bus, PCIIntstatus, 4);
	dhdpcie_bus_cfg_write_dword(bus, PCIIntstatus, 4, status);
	if (!dhd_download_fw_on_driverload)
		dhd_dpc_kill(bus->dhd);

	/* Clear rx control and wake any waiters */
	bus->rxlen = 0;
	dhd_os_ioctl_resp_wake(bus->dhd);

done:
	return;
}

/* Watchdog timer function */
bool dhd_bus_watchdog(dhd_pub_t *dhd)
{
#ifdef DHD_DEBUG
	dhd_bus_t *bus;
	bus = dhd->bus;



	/* Poll for console output periodically */
	if (dhd->busstate == DHD_BUS_DATA && dhd_console_ms != 0) {
		bus->console.count += dhd_watchdog_ms;
		if (bus->console.count >= dhd_console_ms) {
			bus->console.count -= dhd_console_ms;
			/* Make sure backplane clock is on */
			if (dhdpcie_bus_readconsole(bus) < 0)
				dhd_console_ms = 0;	/* On error, stop trying */
		}
	}
#endif /* DHD_DEBUG */

	return FALSE;
}



/* Download firmware image and nvram image */
int
dhd_bus_download_firmware(struct dhd_bus *bus, osl_t *osh,
                          char *pfw_path, char *pnv_path)
{
	int ret;

	bus->fw_path = pfw_path;
	bus->nv_path = pnv_path;

	ret = dhdpcie_download_firmware(bus, osh);

	return ret;
}

static int
dhdpcie_download_firmware(struct dhd_bus *bus, osl_t *osh)
{
	int ret = 0;

	DHD_TRACE_HW4(("%s: firmware path=%s, nvram path=%s\n",
		__FUNCTION__, bus->fw_path, bus->nv_path));

	DHD_OS_WAKE_LOCK(bus->dhd);

	ret = _dhdpcie_download_firmware(bus);

	DHD_OS_WAKE_UNLOCK(bus->dhd);
	return ret;
}

static int
dhdpcie_download_code_file(struct dhd_bus *bus, char *pfw_path)
{
	int bcmerror = -1;
	int offset = 0;
	int len;
	void *image = NULL;
	uint8 *memblock = NULL, *memptr;

	DHD_ERROR(("%s: download firmware %s\n", __FUNCTION__, pfw_path));

	/* Should succeed in opening image if it is actually given through registry
	 * entry or in module param.
	 */
	image = dhd_os_open_image(pfw_path);
	if (image == NULL)
		goto err;

	memptr = memblock = MALLOC(bus->dhd->osh, MEMBLOCK + DHD_SDALIGN);
	if (memblock == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory %d bytes\n", __FUNCTION__, MEMBLOCK));
		goto err;
	}
	if ((uint32)(uintptr)memblock % DHD_SDALIGN)
		memptr += (DHD_SDALIGN - ((uint32)(uintptr)memblock % DHD_SDALIGN));

	/* Download image */
	while ((len = dhd_os_get_image_block((char*)memptr, MEMBLOCK, image))) {
		if (len < 0) {
			DHD_ERROR(("%s: dhd_os_get_image_block failed (%d)\n", __FUNCTION__, len));
			bcmerror = BCME_ERROR;
			goto err;
		}
		/* check if CR4 */
		if (si_setcore(bus->sih, ARMCR4_CORE_ID, 0)) {
			/* if address is 0, store the reset instruction to be written in 0 */

			if (offset == 0) {
				bus->resetinstr = *(((uint32*)memptr));
				/* Add start of RAM address to the address given by user */
				offset += bus->dongle_ram_base;
			}
		}

		bcmerror = dhdpcie_bus_membytes(bus, TRUE, offset, memptr, len);
		if (bcmerror) {
			DHD_ERROR(("%s: error %d on writing %d membytes at 0x%08x\n",
			        __FUNCTION__, bcmerror, MEMBLOCK, offset));
			goto err;
		}

		offset += MEMBLOCK;
	}

err:
	if (memblock)
		MFREE(bus->dhd->osh, memblock, MEMBLOCK + DHD_SDALIGN);

	if (image)
		dhd_os_close_image(image);

	return bcmerror;
}


static int
dhdpcie_download_nvram(struct dhd_bus *bus)
{
	int bcmerror = -1;
	uint len;
	void * image = NULL;
	char * memblock = NULL;
	char *bufp;
	char *pnv_path;
	bool nvram_file_exists;

	pnv_path = bus->nv_path;

	nvram_file_exists = ((pnv_path != NULL) && (pnv_path[0] != '\0'));
	if (!nvram_file_exists && (bus->nvram_params == NULL))
		return (0);

	if (nvram_file_exists) {
		image = dhd_os_open_image(pnv_path);
		if (image == NULL)
			goto err;
	}

	memblock = MALLOC(bus->dhd->osh, MAX_NVRAMBUF_SIZE);
	if (memblock == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory %d bytes\n",
		           __FUNCTION__, MAX_NVRAMBUF_SIZE));
		goto err;
	}

	/* Download variables */
	if (nvram_file_exists) {
		len = dhd_os_get_image_block(memblock, MAX_NVRAMBUF_SIZE, image);
	}
	else {

		/* nvram is string with null terminated. cannot use strlen */
		len = bus->nvram_params_len;
		ASSERT(len <= MAX_NVRAMBUF_SIZE);
		memcpy(memblock, bus->nvram_params, len);
	}
	if (len > 0 && len < MAX_NVRAMBUF_SIZE) {
		bufp = (char *)memblock;
		bufp[len] = 0;

		if (nvram_file_exists)
			len = process_nvram_vars(bufp, len);

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
	else {
		DHD_ERROR(("%s: error reading nvram file: %d\n",
		           __FUNCTION__, len));
		bcmerror = BCME_ERROR;
	}

err:
	if (memblock)
		MFREE(bus->dhd->osh, memblock, MAX_NVRAMBUF_SIZE);

	if (image)
		dhd_os_close_image(image);

	return bcmerror;
}


#ifdef BCMEMBEDIMAGE
int
dhdpcie_download_code_array(struct dhd_bus *bus)
{
	int bcmerror = -1;
	int offset = 0;
	unsigned char *p_dlarray  = NULL;
	unsigned int dlarray_size = 0;
	unsigned int downloded_len, remaining_len, len;
	char *p_dlimagename, *p_dlimagever, *p_dlimagedate;
	uint8 *memblock = NULL, *memptr;

	downloded_len = 0;
	remaining_len = 0;
	len = 0;

	p_dlarray = dlarray;
	dlarray_size = sizeof(dlarray);
	p_dlimagename = dlimagename;
	p_dlimagever  = dlimagever;
	p_dlimagedate = dlimagedate;

	if ((p_dlarray == 0) ||	(dlarray_size == 0) ||(dlarray_size > bus->ramsize) ||
		(p_dlimagename == 0) ||	(p_dlimagever  == 0) ||	(p_dlimagedate == 0))
		goto err;

	memptr = memblock = MALLOC(bus->dhd->osh, MEMBLOCK + DHD_SDALIGN);
	if (memblock == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory %d bytes\n", __FUNCTION__, MEMBLOCK));
		goto err;
	}
	if ((uint32)(uintptr)memblock % DHD_SDALIGN)
		memptr += (DHD_SDALIGN - ((uint32)(uintptr)memblock % DHD_SDALIGN));

	while (downloded_len  < dlarray_size) {
		remaining_len = dlarray_size - downloded_len;
		if (remaining_len >= MEMBLOCK)
			len = MEMBLOCK;
		else
			len = remaining_len;

		memcpy(memptr, (p_dlarray + downloded_len), len);
		/* check if CR4 */
		if (si_setcore(bus->sih, ARMCR4_CORE_ID, 0)) {
			/* if address is 0, store the reset instruction to be written in 0 */
			if (offset == 0) {
				bus->resetinstr = *(((uint32*)memptr));
				/* Add start of RAM address to the address given by user */
				offset += bus->dongle_ram_base;
			}
		}
		bcmerror = dhdpcie_bus_membytes(bus, TRUE, offset, (uint8 *)memptr, len);
		downloded_len += len;
		if (bcmerror) {
			DHD_ERROR(("%s: error %d on writing %d membytes at 0x%08x\n",
				__FUNCTION__, bcmerror, MEMBLOCK, offset));
			goto err;
		}
		offset += MEMBLOCK;
	}

#ifdef DHD_DEBUG
	/* Upload and compare the downloaded code */
	{
		unsigned char *ularray = NULL;
		unsigned int uploded_len;
		uploded_len = 0;
		bcmerror = -1;
		ularray = MALLOC(bus->dhd->osh, dlarray_size);
		if (ularray == NULL)
			goto upload_err;
		/* Upload image to verify downloaded contents. */
		offset = bus->dongle_ram_base;
		memset(ularray, 0xaa, dlarray_size);
		while (uploded_len  < dlarray_size) {
			remaining_len = dlarray_size - uploded_len;
			if (remaining_len >= MEMBLOCK)
				len = MEMBLOCK;
			else
				len = remaining_len;
			bcmerror = dhdpcie_bus_membytes(bus, FALSE, offset,
				(uint8 *)(ularray + uploded_len), len);
			if (bcmerror) {
				DHD_ERROR(("%s: error %d on reading %d membytes at 0x%08x\n",
					__FUNCTION__, bcmerror, MEMBLOCK, offset));
				goto upload_err;
			}

			uploded_len += len;
			offset += MEMBLOCK;
		}

		if (memcmp(p_dlarray, ularray, dlarray_size)) {
			DHD_ERROR(("%s: Downloaded image is corrupted (%s, %s, %s).\n",
				__FUNCTION__, p_dlimagename, p_dlimagever, p_dlimagedate));
			goto upload_err;

		} else
			DHD_ERROR(("%s: Download, Upload and compare succeeded (%s, %s, %s).\n",
				__FUNCTION__, p_dlimagename, p_dlimagever, p_dlimagedate));
upload_err:
		if (ularray)
			MFREE(bus->dhd->osh, ularray, dlarray_size);
	}
#endif /* DHD_DEBUG */
err:

	if (memblock)
		MFREE(bus->dhd->osh, memblock, MEMBLOCK + DHD_SDALIGN);

	return bcmerror;
}
#endif /* BCMEMBEDIMAGE */


static int
_dhdpcie_download_firmware(struct dhd_bus *bus)
{
	int bcmerror = -1;

	bool embed = FALSE;	/* download embedded firmware */
	bool dlok = FALSE;	/* download firmware succeeded */

	/* Out immediately if no image to download */
	if ((bus->fw_path == NULL) || (bus->fw_path[0] == '\0')) {
#ifdef BCMEMBEDIMAGE
		embed = TRUE;
#else
		DHD_ERROR(("%s: no fimrware file\n", __FUNCTION__));
		return 0;
#endif
	}

	/* Keep arm in reset */
	if (dhdpcie_bus_download_state(bus, TRUE)) {
		DHD_ERROR(("%s: error placing ARM core in reset\n", __FUNCTION__));
		goto err;
	}

	/* External image takes precedence if specified */
	if ((bus->fw_path != NULL) && (bus->fw_path[0] != '\0')) {
		if (dhdpcie_download_code_file(bus, bus->fw_path)) {
			DHD_ERROR(("%s: dongle image file download failed\n", __FUNCTION__));
#ifdef BCMEMBEDIMAGE
			embed = TRUE;
#else
			goto err;
#endif
		}
		else {
			embed = FALSE;
			dlok = TRUE;
		}
	}

#ifdef BCMEMBEDIMAGE
	if (embed) {
		if (dhdpcie_download_code_array(bus)) {
			DHD_ERROR(("%s: dongle image array download failed\n", __FUNCTION__));
			goto err;
		}
		else {
			dlok = TRUE;
		}
	}
#else
	BCM_REFERENCE(embed);
#endif
	if (!dlok) {
		DHD_ERROR(("%s: dongle image download failed\n", __FUNCTION__));
		goto err;
	}

	/* EXAMPLE: nvram_array */
	/* If a valid nvram_arry is specified as above, it can be passed down to dongle */
	/* dhd_bus_set_nvram_params(bus, (char *)&nvram_array); */


	/* External nvram takes precedence if specified */
	if (dhdpcie_download_nvram(bus)) {
		DHD_ERROR(("%s: dongle nvram file download failed\n", __FUNCTION__));
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
}

int dhd_bus_rxctl(struct dhd_bus *bus, uchar *msg, uint msglen)
{
	int timeleft;
	uint rxlen = 0;
	bool pending;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus->dhd->dongle_reset)
		return -EIO;

	/* Wait until control frame is available */
	timeleft = dhd_os_ioctl_resp_wait(bus->dhd, &bus->rxlen, &pending);
	if (timeleft == 0) {
		DHD_ERROR(("%s: resumed on timeout\n", __FUNCTION__));
		bus->ioct_resp.cmn_hdr.request_id = 0;
		bus->ioct_resp.compl_hdr.status = 0xffff;
		bus->rxlen = 0;
	}
	rxlen = bus->rxlen;
	bcopy(&bus->ioct_resp, msg, sizeof(ioctl_comp_resp_msg_t));
	bus->rxlen = 0;

	if (rxlen) {
		DHD_CTL(("%s: resumed on rxctl frame, got %d\n", __FUNCTION__, rxlen));
	} else if (timeleft == 0) {
		DHD_ERROR(("%s: resumed on timeout\n", __FUNCTION__));
	} else if (pending == TRUE) {
		DHD_CTL(("%s: canceled\n", __FUNCTION__));
		return -ERESTARTSYS;
	} else {
		DHD_CTL(("%s: resumed for unknown reason?\n", __FUNCTION__));
	}
	if (timeleft == 0) {
		bus->dhd->rxcnt_timeout++;
		DHD_ERROR(("%s: rxcnt_timeout=%d\n", __FUNCTION__, bus->dhd->rxcnt_timeout));
	}
	else
		bus->dhd->rxcnt_timeout = 0;

	if (rxlen)
		bus->dhd->rx_ctlpkts++;
	else
		bus->dhd->rx_ctlerrs++;

	if (bus->dhd->rxcnt_timeout >= MAX_CNTL_TX_TIMEOUT)
		return -ETIMEDOUT;

	if (bus->dhd->dongle_trap_occured)
		return -EREMOTEIO;

	return rxlen ? (int)rxlen : -EIO;

}

#define CONSOLE_LINE_MAX	192

#ifdef DHD_DEBUG
static int
dhdpcie_bus_readconsole(dhd_bus_t *bus)
{
	dhd_console_t *c = &bus->console;
	uint8 line[CONSOLE_LINE_MAX], ch;
	uint32 n, idx, addr;
	int rv;

	/* Don't do anything until FWREADY updates console address */
	if (bus->console_addr == 0)
		return -1;

	/* Read console log struct */
	addr = bus->console_addr + OFFSETOF(hnd_cons_t, log);

	if ((rv = dhdpcie_bus_membytes(bus, FALSE, addr, (uint8 *)&c->log, sizeof(c->log))) < 0)
		return rv;

	/* Allocate console buffer (one time only) */
	if (c->buf == NULL) {
		c->bufsize = ltoh32(c->log.buf_size);
		if ((c->buf = MALLOC(bus->dhd->osh, c->bufsize)) == NULL)
			return BCME_NOMEM;
	}
	idx = ltoh32(c->log.idx);

	/* Protect against corrupt value */
	if (idx > c->bufsize)
		return BCME_ERROR;

	/* Skip reading the console buffer if the index pointer has not moved */
	if (idx == c->last)
		return BCME_OK;

	/* Read the console buffer */
	addr = ltoh32(c->log.buf);
	if ((rv = dhdpcie_bus_membytes(bus, FALSE, addr, c->buf, c->bufsize)) < 0)
		return rv;

	while (c->last != idx) {
		for (n = 0; n < CONSOLE_LINE_MAX - 2; n++) {
			if (c->last == idx) {
				/* This would output a partial line.  Instead, back up
				 * the buffer pointer and output this line next time around.
				 */
				if (c->last >= n)
					c->last -= n;
				else
					c->last = c->bufsize - n;
				goto break2;
			}
			ch = c->buf[c->last];
			c->last = (c->last + 1) % c->bufsize;
			if (ch == '\n')
				break;
			line[n] = ch;
		}

		if (n > 0) {
			if (line[n - 1] == '\r')
				n--;
			line[n] = 0;
			printf("CONSOLE: %s\n", line);
		}
	}
break2:

	return BCME_OK;
}

static int
dhdpcie_checkdied(dhd_bus_t *bus, char *data, uint size)
{
	int bcmerror = 0;
	uint msize = 512;
	char *mbuffer = NULL;
	char *console_buffer = NULL;
	uint maxstrlen = 256;
	char *str = NULL;
	trap_t tr;
	pciedev_shared_t *pciedev_shared = bus->pcie_sh;
	struct bcmstrbuf strbuf;
	uint32 console_ptr, console_size, console_index;
	uint8 line[CONSOLE_LINE_MAX], ch;
	uint32 n, i, addr;
	int rv;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (DHD_NOCHECKDIED_ON())
		return 0;

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
			goto done;
		}
	}

	if ((str = MALLOC(bus->dhd->osh, maxstrlen)) == NULL) {
		DHD_ERROR(("%s: MALLOC(%d) failed \n", __FUNCTION__, maxstrlen));
		bcmerror = BCME_NOMEM;
		goto done;
	}

	if ((bcmerror = dhdpcie_readshared(bus)) < 0)
		goto done;

	bcm_binit(&strbuf, data, size);

	bcm_bprintf(&strbuf, "msgtrace address : 0x%08X\nconsole address  : 0x%08X\n",
	            pciedev_shared->msgtrace_addr, pciedev_shared->console_addr);

	if ((pciedev_shared->flags & PCIE_SHARED_ASSERT_BUILT) == 0) {
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
				                                 (uint8 *)str, maxstrlen)) < 0)
					goto done;

				str[maxstrlen - 1] = '\0';
				bcm_bprintf(&strbuf, " expr \"%s\"", str);
			}

			if (bus->pcie_sh->assert_file_addr != 0) {
				str[0] = '\0';
				if ((bcmerror = dhdpcie_bus_membytes(bus, FALSE,
				                                  bus->pcie_sh->assert_file_addr,
				                                 (uint8 *)str, maxstrlen)) < 0)
					goto done;

				str[maxstrlen - 1] = '\0';
				bcm_bprintf(&strbuf, " file \"%s\"", str);
			}

			bcm_bprintf(&strbuf, " line %d ",  bus->pcie_sh->assert_line);
		}

		if (bus->pcie_sh->flags & PCIE_SHARED_TRAP) {
			bus->dhd->dongle_trap_occured = TRUE;
			if ((bcmerror = dhdpcie_bus_membytes(bus, FALSE,
			                                  bus->pcie_sh->trap_addr,
			                                 (uint8*)&tr, sizeof(trap_t))) < 0)
				goto done;

			bcm_bprintf(&strbuf,
			"Dongle trap type 0x%x @ epc 0x%x, cpsr 0x%x, spsr 0x%x, sp 0x%x,"
			            "lp 0x%x, rpc 0x%x Trap offset 0x%x, "
			"r0 0x%x, r1 0x%x, r2 0x%x, r3 0x%x, "
			"r4 0x%x, r5 0x%x, r6 0x%x, r7 0x%x\n\n",
			ltoh32(tr.type), ltoh32(tr.epc), ltoh32(tr.cpsr), ltoh32(tr.spsr),
			ltoh32(tr.r13), ltoh32(tr.r14), ltoh32(tr.pc),
			ltoh32(bus->pcie_sh->trap_addr),
			ltoh32(tr.r0), ltoh32(tr.r1), ltoh32(tr.r2), ltoh32(tr.r3),
			ltoh32(tr.r4), ltoh32(tr.r5), ltoh32(tr.r6), ltoh32(tr.r7));

			addr =  bus->pcie_sh->console_addr + OFFSETOF(hnd_cons_t, log);
			if ((rv = dhdpcie_bus_membytes(bus, FALSE, addr,
				(uint8 *)&console_ptr, sizeof(console_ptr))) < 0)
				goto printbuf;

			addr =  bus->pcie_sh->console_addr + OFFSETOF(hnd_cons_t, log.buf_size);
			if ((rv = dhdpcie_bus_membytes(bus, FALSE, addr,
				(uint8 *)&console_size, sizeof(console_size))) < 0)
				goto printbuf;

			addr =  bus->pcie_sh->console_addr + OFFSETOF(hnd_cons_t, log.idx);
			if ((rv = dhdpcie_bus_membytes(bus, FALSE, addr,
				(uint8 *)&console_index, sizeof(console_index))) < 0)
				goto printbuf;

			console_ptr = ltoh32(console_ptr);
			console_size = ltoh32(console_size);
			console_index = ltoh32(console_index);

			if (console_size > CONSOLE_BUFFER_MAX ||
				!(console_buffer = MALLOC(bus->dhd->osh, console_size)))
				goto printbuf;

			if ((rv = dhdpcie_bus_membytes(bus, FALSE, console_ptr,
				(uint8 *)console_buffer, console_size)) < 0)
				goto printbuf;

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

					if (dhd_msg_level & DHD_ERROR_VAL)
						printf("CONSOLE: %s\n", line);
				}
			}
		}
	}

printbuf:
	if (bus->pcie_sh->flags & (PCIE_SHARED_ASSERT | PCIE_SHARED_TRAP)) {
		DHD_ERROR(("%s: %s\n", __FUNCTION__, strbuf.origbuf));
	}

done:
	if (mbuffer)
		MFREE(bus->dhd->osh, mbuffer, msize);
	if (str)
		MFREE(bus->dhd->osh, str, maxstrlen);

	if (console_buffer)
		MFREE(bus->dhd->osh, console_buffer, console_size);

	return bcmerror;
}
#endif /* DHD_DEBUG */


/**
 * Transfers bytes from host to dongle using pio mode.
 * Parameter 'address' is a backplane address.
 */
static int
dhdpcie_bus_membytes(dhd_bus_t *bus, bool write, ulong address, uint8 *data, uint size)
{
	int bcmerror = 0;
	uint dsize;
	int detect_endian_flag = 0x01;
	bool little_endian;

	/* Detect endianness. */
	little_endian = *(char *)&detect_endian_flag;

	/* In remap mode, adjust address beyond socram and redirect
	 * to devram at SOCDEVRAM_BP_ADDR since remap address > orig_ramsize
	 * is not backplane accessible
	 */

	/* Determine initial transfer parameters */
	dsize = sizeof(uint64);

	/* Do the transfer(s) */
	if (write) {
		while (size) {
			if (size >= sizeof(uint64) && little_endian)
				dhdpcie_bus_wtcm64(bus, address, *((uint64 *)data));
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
			if (size >= sizeof(uint64) && little_endian)
				*(uint64 *)data = dhdpcie_bus_rtcm64(bus, address);
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
	return bcmerror;
}

int BCMFASTPATH
dhd_bus_schedule_queue(struct dhd_bus  *bus, uint16 flow_id, bool txs)
{
	flow_ring_node_t *flow_ring_node;
	int ret = BCME_OK;

	DHD_INFO(("%s: flow_id is %d\n", __FUNCTION__, flow_id));
	/* ASSERT on flow_id */
	if (flow_id >= bus->max_sub_queues) {
		DHD_ERROR(("%s: flow_id is invalid %d, max %d\n", __FUNCTION__,
			flow_id, bus->max_sub_queues));
		return 0;
	}

	flow_ring_node = DHD_FLOW_RING(bus->dhd, flow_id);

	{
		unsigned long flags;
		void *txp = NULL;
		flow_queue_t *queue;

		queue = &flow_ring_node->queue; /* queue associated with flow ring */

		DHD_QUEUE_LOCK(queue->lock, flags);

		while ((txp = dhd_flow_queue_dequeue(bus->dhd, queue)) != NULL) {
			PKTORPHAN(txp);

#ifdef DHDTCPACK_SUPPRESS
		dhd_tcpack_check_xmit(bus->dhd, txp);
#endif /* DHDTCPACK_SUPPRESS */
			/* Attempt to transfer packet over flow ring */

			ret = dhd_prot_txdata(bus->dhd, txp, flow_ring_node->flow_info.ifindex);
			if (ret != BCME_OK) { /* may not have resources in flow ring */
				DHD_INFO(("%s: Reinserrt %d\n", __FUNCTION__, ret));
				dhd_prot_txdata_write_flush(bus->dhd, flow_id, FALSE);
				/* reinsert at head */
				dhd_flow_queue_reinsert(bus->dhd, queue, txp);
				DHD_QUEUE_UNLOCK(queue->lock, flags);

				/* If we are able to requeue back, return success */
				return BCME_OK;
			}
		}

		dhd_prot_txdata_write_flush(bus->dhd, flow_id, FALSE);

		DHD_QUEUE_UNLOCK(queue->lock, flags);
	}

	return ret;
}

/* Send a data frame to the dongle.  Callee disposes of txp. */
int BCMFASTPATH
dhd_bus_txdata(struct dhd_bus *bus, void *txp, uint8 ifidx)
{
	unsigned long flags;
	int ret = BCME_OK;
	void *txp_pend = NULL;
	if (!bus->txmode_push) {
		uint16 flowid;
		flow_queue_t *queue;
		flow_ring_node_t *flow_ring_node;
		if (!bus->dhd->flowid_allocator) {
			DHD_ERROR(("%s: Flow ring not intited yet  \n", __FUNCTION__));
			goto toss;
		}

		flowid = DHD_PKTTAG_FLOWID((dhd_pkttag_fr_t*)PKTTAG(txp));

		flow_ring_node = DHD_FLOW_RING(bus->dhd, flowid);

		DHD_TRACE(("%s: pkt flowid %d, status %d active %d\n",
			__FUNCTION__, flowid, flow_ring_node->status,
			flow_ring_node->active));

		if ((flowid >= bus->dhd->num_flow_rings) ||
			(!flow_ring_node->active) ||
			(flow_ring_node->status == FLOW_RING_STATUS_DELETE_PENDING)) {
			DHD_INFO(("%s: Dropping pkt flowid %d, status %d active %d\n",
				__FUNCTION__, flowid, flow_ring_node->status,
				flow_ring_node->active));
			ret = BCME_ERROR;
			goto toss;
		}

		queue = &flow_ring_node->queue; /* queue associated with flow ring */

		DHD_QUEUE_LOCK(queue->lock, flags);

		if ((ret = dhd_flow_queue_enqueue(bus->dhd, queue, txp)) != BCME_OK)
			txp_pend = txp;

		DHD_QUEUE_UNLOCK(queue->lock, flags);

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
		ret = dhd_bus_schedule_queue(bus, flowid, FALSE);

		/* If we have anything pending, try to push into q */
		if (txp_pend) {
			DHD_QUEUE_LOCK(queue->lock, flags);

			if ((ret = dhd_flow_queue_enqueue(bus->dhd, queue, txp_pend)) != BCME_OK) {
				DHD_QUEUE_UNLOCK(queue->lock, flags);
				txp = txp_pend;
				goto toss;
			}

			DHD_QUEUE_UNLOCK(queue->lock, flags);
		}

		return ret;

	} else { /* bus->txmode_push */
		return dhd_prot_txdata(bus->dhd, txp, ifidx);
	}

toss:
	DHD_INFO(("%s: Toss %d\n", __FUNCTION__, ret));
	PKTCFREE(bus->dhd->osh, txp, TRUE);
	return ret;
}


void
dhd_bus_stop_queue(struct dhd_bus *bus)
{
	dhd_txflowcontrol(bus->dhd, ALL_INTERFACES, ON);
	bus->bus_flowctrl = TRUE;
}

void
dhd_bus_start_queue(struct dhd_bus *bus)
{
	dhd_txflowcontrol(bus->dhd, ALL_INTERFACES, OFF);
	bus->bus_flowctrl = TRUE;
}

void
dhd_bus_update_retlen(dhd_bus_t *bus, uint32 retlen, uint32 pkt_id, uint16 status,
	uint32 resp_len)
{
	bus->rxlen = retlen;
	bus->ioct_resp.cmn_hdr.request_id = pkt_id;
	bus->ioct_resp.compl_hdr.status = status;
	bus->ioct_resp.resp_len = (uint16)resp_len;
}

#if defined(DHD_DEBUG)
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
		dhd_os_sdunlock(bus->dhd);
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

	/* generate an interurpt to dongle to indicate that it needs to process cons command */
	dhdpcie_send_mb_data(bus, H2D_HOST_CONS_INT);
done:
	return rv;
}
#endif /* defined(DHD_DEBUG) */

/* Process rx frame , Send up the layer to netif */
void BCMFASTPATH
dhd_bus_rx_frame(struct dhd_bus *bus, void* pkt, int ifidx, uint pkt_count)
{
	dhd_rx_frame(bus->dhd, ifidx, pkt, pkt_count, 0);
}

/** 'offset' is a backplane address */
void
dhdpcie_bus_wtcm8(dhd_bus_t *bus, ulong offset, uint8 data)
{
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_set_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
	*(volatile uint8 *)(bus->tcm + offset) = (uint8)data;
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_clear_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
}

uint8
dhdpcie_bus_rtcm8(dhd_bus_t *bus, ulong offset)
{
	volatile uint8 data;
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_set_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
#ifdef BCM47XX_ACP_WAR
	data = R_REG(bus->dhd->osh, (volatile uint8 *)(bus->tcm + offset));
#else
	data = *(volatile uint8 *)(bus->tcm + offset);
#endif
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_clear_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
	return data;
}

void
dhdpcie_bus_wtcm32(dhd_bus_t *bus, ulong offset, uint32 data)
{
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_set_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
	*(volatile uint32 *)(bus->tcm + offset) = (uint32)data;
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_clear_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
}
void
dhdpcie_bus_wtcm16(dhd_bus_t *bus, ulong offset, uint16 data)
{
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_set_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
	*(volatile uint16 *)(bus->tcm + offset) = (uint16)data;
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_clear_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
}
void
dhdpcie_bus_wtcm64(dhd_bus_t *bus, ulong offset, uint64 data)
{
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_set_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
	*(volatile uint64 *)(bus->tcm + offset) = (uint64)data;
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_clear_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
}

uint16
dhdpcie_bus_rtcm16(dhd_bus_t *bus, ulong offset)
{
	volatile uint16 data;
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_set_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
#ifdef BCM47XX_ACP_WAR
	data = R_REG(bus->dhd->osh, (volatile uint16 *)(bus->tcm + offset));
#else
	data = *(volatile uint16 *)(bus->tcm + offset);
#endif
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_clear_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
	return data;
}

uint32
dhdpcie_bus_rtcm32(dhd_bus_t *bus, ulong offset)
{
	volatile uint32 data;
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_set_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
#ifdef BCM47XX_ACP_WAR
	data = R_REG(bus->dhd->osh, (volatile uint32 *)(bus->tcm + offset));
#else
	data = *(volatile uint32 *)(bus->tcm + offset);
#endif
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_clear_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
	return data;
}

uint64
dhdpcie_bus_rtcm64(dhd_bus_t *bus, ulong offset)
{
	volatile uint64 data;
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_set_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
#ifdef BCM47XX_ACP_WAR
	data = R_REG(bus->dhd->osh, (volatile uint64 *)(bus->tcm + offset));
#else
	data = *(volatile uint64 *)(bus->tcm + offset);
#endif
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_clear_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
	return data;
}

void
dhd_bus_cmn_writeshared(dhd_bus_t *bus, void * data, uint32 len, uint8 type, uint16 ringid)
{
	uint64 long_data;
	ulong tcm_offset;
	pciedev_shared_t *sh;
	pciedev_shared_t *shmem = NULL;

	sh = (pciedev_shared_t*)bus->shared_addr;

	DHD_INFO(("%s: writing to msgbuf type %d, len %d\n", __FUNCTION__, type, len));

	switch (type) {
		case DNGL_TO_HOST_DMA_SCRATCH_BUFFER:
			long_data = HTOL64(*(uint64 *)data);
			tcm_offset = (ulong)&(sh->host_dma_scratch_buffer);
			dhdpcie_bus_membytes(bus, TRUE, tcm_offset, (uint8*) &long_data, len);
			prhex(__FUNCTION__, data, len);
			break;

		case DNGL_TO_HOST_DMA_SCRATCH_BUFFER_LEN :
			tcm_offset = (ulong)&(sh->host_dma_scratch_buffer_len);
			dhdpcie_bus_wtcm32(bus, tcm_offset, (uint32) HTOL32(*(uint32 *)data));
			prhex(__FUNCTION__, data, len);
			break;

		case HOST_TO_DNGL_DMA_WRITEINDX_BUFFER:
			/* ring_info_ptr stored in pcie_sh */
			shmem = (pciedev_shared_t *)bus->pcie_sh;

			long_data = HTOL64(*(uint64 *)data);
			tcm_offset = (ulong)shmem->rings_info_ptr;
			tcm_offset += OFFSETOF(ring_info_t, h2d_w_idx_hostaddr);
			dhdpcie_bus_membytes(bus, TRUE, tcm_offset, (uint8*) &long_data, len);
			prhex(__FUNCTION__, data, len);
			break;

		case HOST_TO_DNGL_DMA_READINDX_BUFFER:
			/* ring_info_ptr stored in pcie_sh */
			shmem = (pciedev_shared_t *)bus->pcie_sh;

			long_data = HTOL64(*(uint64 *)data);
			tcm_offset = (ulong)shmem->rings_info_ptr;
			tcm_offset += OFFSETOF(ring_info_t, h2d_r_idx_hostaddr);
			dhdpcie_bus_membytes(bus, TRUE, tcm_offset, (uint8*) &long_data, len);
			prhex(__FUNCTION__, data, len);
			break;

		case DNGL_TO_HOST_DMA_WRITEINDX_BUFFER:
			/* ring_info_ptr stored in pcie_sh */
			shmem = (pciedev_shared_t *)bus->pcie_sh;

			long_data = HTOL64(*(uint64 *)data);
			tcm_offset = (ulong)shmem->rings_info_ptr;
			tcm_offset += OFFSETOF(ring_info_t, d2h_w_idx_hostaddr);
			dhdpcie_bus_membytes(bus, TRUE, tcm_offset, (uint8*) &long_data, len);
			prhex(__FUNCTION__, data, len);
			break;

		case DNGL_TO_HOST_DMA_READINDX_BUFFER:
			/* ring_info_ptr stored in pcie_sh */
			shmem = (pciedev_shared_t *)bus->pcie_sh;

			long_data = HTOL64(*(uint64 *)data);
			tcm_offset = (ulong)shmem->rings_info_ptr;
			tcm_offset += OFFSETOF(ring_info_t, d2h_r_idx_hostaddr);
			dhdpcie_bus_membytes(bus, TRUE, tcm_offset, (uint8*) &long_data, len);
			prhex(__FUNCTION__, data, len);
			break;

		case RING_LEN_ITEMS :
			tcm_offset = bus->ring_sh[ringid].ring_mem_addr;
			tcm_offset += OFFSETOF(ring_mem_t, len_items);
			dhdpcie_bus_wtcm16(bus, tcm_offset, (uint16) HTOL16(*(uint16 *)data));
			break;

		case RING_MAX_ITEM :
			tcm_offset = bus->ring_sh[ringid].ring_mem_addr;
			tcm_offset += OFFSETOF(ring_mem_t, max_item);
			dhdpcie_bus_wtcm16(bus, tcm_offset, (uint16) HTOL16(*(uint16 *)data));
			break;

		case RING_BUF_ADDR :
			long_data = HTOL64(*(uint64 *)data);
			tcm_offset = bus->ring_sh[ringid].ring_mem_addr;
			tcm_offset += OFFSETOF(ring_mem_t, base_addr);
			dhdpcie_bus_membytes(bus, TRUE, tcm_offset, (uint8 *) &long_data, len);
			prhex(__FUNCTION__, data, len);
			break;

		case RING_WRITE_PTR :
			tcm_offset = bus->ring_sh[ringid].ring_state_w;
			dhdpcie_bus_wtcm16(bus, tcm_offset, (uint16) HTOL16(*(uint16 *)data));
			break;
		case RING_READ_PTR :
			tcm_offset = bus->ring_sh[ringid].ring_state_r;
			dhdpcie_bus_wtcm16(bus, tcm_offset, (uint16) HTOL16(*(uint16 *)data));
			break;

		case DTOH_MB_DATA:
			dhdpcie_bus_wtcm32(bus, bus->d2h_mb_data_ptr_addr,
				(uint32) HTOL32(*(uint32 *)data));
			break;

		case HTOD_MB_DATA:
			dhdpcie_bus_wtcm32(bus, bus->h2d_mb_data_ptr_addr,
				(uint32) HTOL32(*(uint32 *)data));
			break;
		default:
			break;
	}
}


void
dhd_bus_cmn_readshared(dhd_bus_t *bus, void* data, uint8 type, uint16 ringid)
{
	pciedev_shared_t *sh;
	ulong tcm_offset;

	sh = (pciedev_shared_t*)bus->shared_addr;

	switch (type) {
		case RING_WRITE_PTR :
			tcm_offset = bus->ring_sh[ringid].ring_state_w;
			*(uint16*)data = LTOH16(dhdpcie_bus_rtcm16(bus, tcm_offset));
			break;
		case RING_READ_PTR :
			tcm_offset = bus->ring_sh[ringid].ring_state_r;
			*(uint16*)data = LTOH16(dhdpcie_bus_rtcm16(bus, tcm_offset));
			break;
		case TOTAL_LFRAG_PACKET_CNT :
			*(uint16*)data = LTOH16(dhdpcie_bus_rtcm16(bus,
				(ulong) &sh->total_lfrag_pkt_cnt));
			break;
		case HTOD_MB_DATA:
			*(uint32*)data = LTOH32(dhdpcie_bus_rtcm32(bus, bus->h2d_mb_data_ptr_addr));
			break;
		case DTOH_MB_DATA:
			*(uint32*)data = LTOH32(dhdpcie_bus_rtcm32(bus, bus->d2h_mb_data_ptr_addr));
			break;
		case MAX_HOST_RXBUFS :
			*(uint16*)data = LTOH16(dhdpcie_bus_rtcm16(bus,
				(ulong) &sh->max_host_rxbufs));
			break;
		default :
			break;
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

int
dhd_bus_iovar_op(dhd_pub_t *dhdp, const char *name,
                 void *params, int plen, void *arg, int len, bool set)
{
	dhd_bus_t *bus = dhdp->bus;
	const bcm_iovar_t *vi = NULL;
	int bcmerror = 0;
	int val_size;
	uint32 actionid;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(name);
	ASSERT(len >= 0);

	/* Get MUST have return space */
	ASSERT(set || (arg && len));

	/* Set does NOT take qualifiers */
	ASSERT(!set || (!params && !plen));

	DHD_INFO(("%s: %s %s, len %d plen %d\n", __FUNCTION__,
	         name, (set ? "set" : "get"), len, plen));

	/* Look up var locally; if not found pass to host driver */
	if ((vi = bcm_iovar_lookup(dhdpcie_iovars, name)) == NULL) {
		goto exit;
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
	return bcmerror;
}

#ifdef BCM_BUZZZ
#include <bcm_buzzz.h>

int dhd_buzzz_dump_cntrs3(char *p, uint32 *core, uint32 * ovhd, uint32 *log)
{
	int bytes = 0;
	uint32 ctr, curr[3], prev[3], delta[3];

	/* Compute elapsed counter values per counter event type */
	for (ctr = 0U; ctr < 3; ctr++) {
		prev[ctr] = core[ctr];
		curr[ctr] = *log++;
		core[ctr] = curr[ctr];  /* saved for next log */

		if (curr[ctr] < prev[ctr])
			delta[ctr] = curr[ctr] + (~0U - prev[ctr]);
		else
			delta[ctr] = (curr[ctr] - prev[ctr]);

		/* Adjust for instrumentation overhead */
		if (delta[ctr] >= ovhd[ctr])
			delta[ctr] -= ovhd[ctr];
		else
			delta[ctr] = 0;

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

int dhd_buzzz_dump_cntrs6(char *p, uint32 *core, uint32 * ovhd, uint32 *log)
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
		if (delta >= ovhd[0])
			delta -= ovhd[0];
		else
			delta = 0;

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
			if (delta.u8[i] >= ovhd[i + 1])
				delta.u8[i] -= ovhd[i + 1];
			else
				delta.u8[i] = 0;
			bytes += sprintf(p + bytes, "%4u ", delta.u8[i]);
		}
		cm3_cnts.u32 = delta.u32;
	}

	{   /* Extract the foldcnt from arg0 */
		uint8 curr, prev, delta, max8 = ~0;
		buzzz_arg0_t arg0; arg0.u32 = *log;
		prev = core[2]; curr = arg0.klog.cnt; core[2] = curr;
		if (curr < prev)
			delta = curr + (max8 - prev);
		else
			delta = (curr - prev);
		if (delta >= ovhd[5])
			delta -= ovhd[5];
		else
			delta = 0;
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

int dhd_buzzz_dump_log(char * p, uint32 * core, uint32 * log, buzzz_t * buzzz)
{
	int bytes = 0;
	buzzz_arg0_t arg0;
	static uint8 * fmt[] = BUZZZ_FMT_STRINGS;

	if (buzzz->counters == 6) {
		bytes += dhd_buzzz_dump_cntrs6(p, core, buzzz->ovhd, log);
		log += 2; /* 32bit cyccnt + (4 x 8bit) CM3 */
	} else {
		bytes += dhd_buzzz_dump_cntrs3(p, core, buzzz->ovhd, log);
		log += 3; /* (3 x 32bit) CR4 */
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
		default:
			printf("Maximum one argument supported\n");
			break;
	}
	bytes += sprintf(p + bytes, "\n");

	return bytes;
}

void dhd_buzzz_dump(buzzz_t * buzzz_p, void * buffer_p, char * p)
{
	int i;
	uint32 total, part1, part2, log_sz, core[BUZZZ_COUNTERS_MAX];
	void * log;

	for (i = 0; i < BUZZZ_COUNTERS_MAX; i++)
		core[i] = 0;

	log_sz = buzzz_p->log_sz;

	part1 = ((uint32)buzzz_p->cur - (uint32)buzzz_p->log) / log_sz;

	if (buzzz_p->wrap == TRUE) {
		part2 = ((uint32)buzzz_p->end - (uint32)buzzz_p->cur) / log_sz;
		total = (buzzz_p->buffer_sz - BUZZZ_LOGENTRY_MAXSZ) / log_sz;
	} else {
		part2 = 0U;
		total = buzzz_p->count;
	}

	if (total == 0U) {
		printf("buzzz_dump total<%u> done\n", total);
		return;
	} else {
		printf("buzzz_dump total<%u> : part2<%u> + part1<%u>\n",
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

	printf("buzzz_dump done.\n");
}

int dhd_buzzz_dump_dngl(dhd_bus_t *bus)
{
	buzzz_t * buzzz_p = NULL;
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
	if ((buzzz_p = MALLOC(bus->dhd->osh, sizeof(buzzz_t))) == NULL) {
		printf("Buzzz memory allocation failure\n");
		goto done;
	}

	ret = dhdpcie_readshared(bus);
	if (ret < 0) {
		DHD_ERROR(("%s :Shared area read failed \n", __FUNCTION__));
		goto done;
	}

	sh = bus->pcie_sh;

	DHD_INFO(("%s buzzz:%08x\n", __FUNCTION__, sh->buzzz));

	if (sh->buzzz != 0U) {	/* Fetch and display dongle BUZZZ Trace */
		dhdpcie_bus_membytes(bus, FALSE, (ulong)sh->buzzz,
		                     (uint8 *)buzzz_p, sizeof(buzzz_t));
		if (buzzz_p->count == 0) {
			printf("Empty dongle BUZZZ trace\n\n");
			goto done;
		}
		if (buzzz_p->counters != 3) { /* 3 counters for CR4 */
			printf("Counters<%u> mismatch\n", buzzz_p->counters);
			goto done;
		}
		/* Allocate memory for trace buffer and format strings */
		buffer_p = MALLOC(bus->dhd->osh, buzzz_p->buffer_sz);
		if (buffer_p == NULL) {
			printf("Buffer memory allocation failure\n");
			goto done;
		}
		/* Fetch the trace and format strings */
		dhdpcie_bus_membytes(bus, FALSE, (uint32)buzzz_p->log,   /* Trace */
		                     (uint8 *)buffer_p, buzzz_p->buffer_sz);
		/* Process and display the trace using formatted output */
		printf("<#cycle> <#instruction> <#ctr3> <event information>\n");
		dhd_buzzz_dump(buzzz_p, buffer_p, page_p);
		printf("----- End of dongle BUZZZ Trace -----\n\n");
		MFREE(bus->dhd->osh, buffer_p, buzzz_p->buffer_sz); buffer_p = NULL;
	}

done:

	if (page_p)   MFREE(bus->dhd->osh, page_p, 4096);
	if (buzzz_p)  MFREE(bus->dhd->osh, buzzz_p, sizeof(buzzz_t));
	if (buffer_p) MFREE(bus->dhd->osh, buffer_p, buzzz_p->buffer_sz);

	return BCME_OK;
}
#endif /* BCM_BUZZZ */

#define PCIE_GEN2(sih) ((BUSTYPE((sih)->bustype) == PCI_BUS) &&	\
	((sih)->buscoretype == PCIE2_CORE_ID))

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
	}
	else
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

int
dhd_bus_devreset(dhd_pub_t *dhdp, uint8 flag)
{
	dhd_bus_t *bus = dhdp->bus;
	int bcmerror = 0;
#ifdef CONFIG_ARCH_MSM
	int retry = POWERUP_MAX_RETRY;
#endif /* CONFIG_ARCH_MSM */

	if (dhd_download_fw_on_driverload) {
		bcmerror = dhd_bus_start(dhdp);
	} else {
		if (flag == TRUE) { /* Turn off WLAN */
			/* Removing Power */
			DHD_ERROR(("%s: == Power OFF ==\n", __FUNCTION__));
			bus->dhd->up = FALSE;
			if (bus->dhd->busstate != DHD_BUS_DOWN) {
				dhd_prot_clear(dhdp);
				dhd_os_wd_timer(dhdp, 0);
				dhd_bus_stop(bus, TRUE);
#ifdef CONFIG_ARCH_MSM
				dhd_bus_release_dongle(bus);
#endif /* CONFIG_ARCH_MSM */
				dhdpcie_bus_free_resource(bus);
				bcmerror = dhdpcie_bus_disable_device(bus);
				if (bcmerror) {
					DHD_ERROR(("%s: dhdpcie_bus_disable_device: %d\n",
						__FUNCTION__, bcmerror));
					goto done;
				}
#ifdef CONFIG_ARCH_MSM
				bcmerror = dhdpcie_bus_clock_stop(bus);
				if (bcmerror) {
					DHD_ERROR(("%s: host clock stop failed: %d\n",
						__FUNCTION__, bcmerror));
					goto done;
				}
#endif /* CONFIG_ARCH_MSM */
				bus->dhd->busstate = DHD_BUS_DOWN;
			} else {
				dhd_prot_clear(dhdp);
#ifdef CONFIG_ARCH_MSM
				dhd_bus_release_dongle(bus);
#endif /* CONFIG_ARCH_MSM */
				dhdpcie_bus_free_resource(bus);
				bcmerror = dhdpcie_bus_disable_device(bus);
				if (bcmerror) {
					DHD_ERROR(("%s: dhdpcie_bus_disable_device: %d\n",
						__FUNCTION__, bcmerror));
					goto done;
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
				while (retry--) {
					bcmerror = dhdpcie_bus_clock_start(bus);
					if (!bcmerror) {
						DHD_ERROR(("%s: dhdpcie_bus_clock_start OK\n",
							__FUNCTION__));
						break;
					}
					else
						OSL_SLEEP(10);
				}

				if (bcmerror && !retry) {
					DHD_ERROR(("%s: host pcie clock enable failed: %d\n",
						__FUNCTION__, bcmerror));
					goto done;
				}
#endif /* CONFIG_ARCH_MSM */
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
					DHD_ERROR(("%s: dhdpcie_bus_dongle_attach: %d\n",
						__FUNCTION__, bcmerror));
					goto done;
				}

				bus->dhd->dongle_reset = FALSE;

				bcmerror = dhd_bus_start(dhdp);
				if (bcmerror) {
					DHD_ERROR(("%s: dhd_bus_start: %d\n",
						__FUNCTION__, bcmerror));
					goto done;
				}

				bus->dhd->up = TRUE;
				DHD_ERROR(("%s: WLAN Power On Done\n", __FUNCTION__));
			} else {
				DHD_ERROR(("%s: what should we do here\n", __FUNCTION__));
				goto done;
			}
		}
	}
done:
	if (bcmerror)
		bus->dhd->busstate = DHD_BUS_DOWN;

	return bcmerror;
}

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
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_BAR0_SECWIN_REG):
	{
		uint32 cur_base, base;
		uchar *bar0;
		volatile uint32 *offset;
		/* set the bar0 secondary window to this */
		/* write the register value */
		cur_base = dhdpcie_bus_cfg_read_dword(bus, PCIE2_BAR0_CORE2_WIN, sizeof(uint));
		base = int_val & 0xFFFFF000;
		dhdpcie_bus_cfg_write_dword(bus, PCIE2_BAR0_CORE2_WIN,  sizeof(uint32), base);
		bar0 = (uchar *)bus->regs;
		offset = (uint32 *)(bar0 + 0x4000 + (int_val & 0xFFF));
		int_val = *offset;
		bcopy(&int_val, arg, val_size);
		dhdpcie_bus_cfg_write_dword(bus, PCIE2_BAR0_CORE2_WIN, sizeof(uint32), cur_base);
	}
		break;
	case IOV_SVAL(IOV_BAR0_SECWIN_REG):
	{
		uint32 cur_base, base;
		uchar *bar0;
		volatile uint32 *offset;
		/* set the bar0 secondary window to this */
		/* write the register value */
		cur_base = dhdpcie_bus_cfg_read_dword(bus, PCIE2_BAR0_CORE2_WIN, sizeof(uint));
		base = int_val & 0xFFFFF000;
		dhdpcie_bus_cfg_write_dword(bus, PCIE2_BAR0_CORE2_WIN,  sizeof(uint32), base);
		bar0 = (uchar *)bus->regs;
		offset = (uint32 *)(bar0 + 0x4000 + (int_val & 0xFFF));
		*offset = int_val2;
		bcopy(&int_val2, arg, val_size);
		dhdpcie_bus_cfg_write_dword(bus, PCIE2_BAR0_CORE2_WIN, sizeof(uint32), cur_base);
	}
		break;

	case IOV_SVAL(IOV_PCIECOREREG):
		si_corereg(bus->sih, bus->sih->buscoreidx, int_val, ~0, int_val2);
		break;
	case IOV_GVAL(IOV_SBREG):
	{
		sdreg_t sdreg;
		uint32 addr, coreidx;

		bcopy(params, &sdreg, sizeof(sdreg));

		addr = sdreg.offset;
		coreidx =  (addr & 0xF000) >> 12;

		int_val = si_corereg(bus->sih, coreidx, (addr & 0xFFF), 0, 0);
		bcopy(&int_val, arg, sizeof(int32));
		break;
	}

	case IOV_SVAL(IOV_SBREG):
	{
		sdreg_t sdreg;
		uint32 addr, coreidx;

		bcopy(params, &sdreg, sizeof(sdreg));

		addr = sdreg.offset;
		coreidx =  (addr & 0xF000) >> 12;

		si_corereg(bus->sih, coreidx, (addr & 0xFFF), ~0, sdreg.value);

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
		}
		else {
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
		if (pcie2_mdioop(bus, int_val, int_val2, TRUE, &int_val3, FALSE)) {
			DHD_ERROR(("pcie2_mdioop failed.\n"));
			bcmerror = BCME_ERROR;
		}
		break;
	case IOV_GVAL(IOV_PCIECOREREG):
		int_val = si_corereg(bus->sih, bus->sih->buscoreidx, int_val, 0, 0);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_PCIECFGREG):
		OSL_PCI_WRITE_CONFIG(bus->osh, int_val, 4, int_val2);
		break;

	case IOV_GVAL(IOV_PCIECFGREG):
		int_val = OSL_PCI_READ_CONFIG(bus->osh, int_val, 4);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_PCIE_LPBK):
		bcmerror = dhdpcie_bus_lpback_req(bus, int_val);
		break;

	case IOV_SVAL(IOV_PCIE_DMAXFER):
		bcmerror = dhdpcie_bus_dmaxfer_req(bus, int_val, int_val2, int_val3);
		break;

	case IOV_GVAL(IOV_PCIE_SUSPEND):
		int_val = (bus->dhd->busstate == DHD_BUS_SUSPEND) ? 1 : 0;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_PCIE_SUSPEND):
		dhdpcie_bus_suspend(bus, bool_val);
		break;

	case IOV_GVAL(IOV_MEMSIZE):
		int_val = (int32)bus->ramsize;
		bcopy(&int_val, arg, val_size);
		break;
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
		if (si_setcore(bus->sih, ARMCR4_CORE_ID, 0)) {
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

#ifdef BCM_BUZZZ
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
	{	int h2d_support, d2h_support;

		d2h_support = DMA_INDX_ENAB(bus->dhd->dma_d2h_ring_upd_support) ? 1 : 0;
		h2d_support = DMA_INDX_ENAB(bus->dhd->dma_h2d_ring_upd_support) ? 1 : 0;
		int_val = d2h_support | (h2d_support << 1);
		bcopy(&int_val, arg, val_size);
		break;
	}
	case IOV_SVAL(IOV_DMA_RINGINDICES):
		/* Can change it only during initialization/FW download */
		if (bus->dhd->busstate == DHD_BUS_DOWN) {
			if ((int_val > 3) || (int_val < 0)) {
				DHD_ERROR(("Bad argument. Possible values: 0, 1, 2 & 3\n"));
				bcmerror = BCME_BADARG;
			} else {
				bus->dhd->dma_d2h_ring_upd_support = (int_val & 1) ? TRUE : FALSE;
				bus->dhd->dma_h2d_ring_upd_support = (int_val & 2) ? TRUE : FALSE;
			}
		} else {
			DHD_ERROR(("%s: Can change only when bus down (before FW download)\n",
				__FUNCTION__));
			bcmerror = BCME_NOTDOWN;
		}
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

	case IOV_GVAL(IOV_FLOW_PRIO_MAP):
		int_val = bus->dhd->flow_prio_map_type;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_FLOW_PRIO_MAP):
		int_val = (int32)dhd_update_flow_prio_map(bus->dhd, (uint8)int_val);
		bcopy(&int_val, arg, val_size);
		break;

	default:
		bcmerror = BCME_UNSUPPORTED;
		break;
	}

exit:
	return bcmerror;
}

/* Transfers bytes from host to dongle using pio mode */
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
dhd_bus_set_suspend_resume(dhd_pub_t *dhdp, bool state)
{
	struct  dhd_bus *bus = dhdp->bus;
	if (bus) {
		dhdpcie_bus_suspend(bus, state);
	}
}

int
dhdpcie_bus_suspend(struct  dhd_bus *bus, bool state)
{

	int timeleft;
	bool pending;
	int rc = 0;

	if (bus->dhd == NULL) {
		DHD_ERROR(("bus not inited\n"));
		return BCME_ERROR;
	}
	if (bus->dhd->prot == NULL) {
		DHD_ERROR(("prot is not inited\n"));
		return BCME_ERROR;
	}
	if (bus->dhd->busstate != DHD_BUS_DATA && bus->dhd->busstate != DHD_BUS_SUSPEND) {
		DHD_ERROR(("not in a readystate to LPBK  is not inited\n"));
		return BCME_ERROR;
	}
	if (bus->dhd->dongle_reset)
		return -EIO;

	if (state == (bus->dhd->busstate == DHD_BUS_SUSPEND)) /* Set to same state */
		return BCME_OK;

	if (state) {
#ifdef EXYNOS5433_PCIE_WAR
		exynos_pcie_set_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
		bus->wait_for_d3_ack = 0;
		DHD_OS_WAKE_LOCK_WAIVE(bus->dhd);
		dhdpcie_send_mb_data(bus, H2D_HOST_D3_INFORM);
		timeleft = dhd_os_ioctl_resp_wait(bus->dhd, &bus->wait_for_d3_ack, &pending);
		DHD_OS_WAKE_LOCK_RESTORE(bus->dhd);
		if (bus->wait_for_d3_ack) {
			/* Got D3 Ack. Suspend the bus */
			rc = dhdpcie_pci_suspend_resume(bus->dev, state);
			bus->dhd->busstate = DHD_BUS_SUSPEND;
		} else if (timeleft == 0) {
			DHD_ERROR(("%s: resumed on timeout\n", __FUNCTION__));
#ifdef EXYNOS5433_PCIE_WAR
			exynos_pcie_clear_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
			return -ETIMEDOUT;
		}
		bus->wait_for_d3_ack = 1;
	}
	else {
		/* Resume */
		rc = dhdpcie_pci_suspend_resume(bus->dev, state);
		bus->dhd->busstate = DHD_BUS_DATA;

	}
#ifdef EXYNOS5433_PCIE_WAR
	exynos_pcie_clear_l1_exit();
#endif /* EXYNOS5433_PCIE_WAR */
	return rc;
}

/* Transfers bytes from host to dongle and to host again using DMA */
static int
dhdpcie_bus_dmaxfer_req(struct  dhd_bus *bus, uint32 len, uint32 srcdelay, uint32 destdelay)
{
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
	return dhdmsgbuf_dmaxfer_req(bus->dhd, len, srcdelay, destdelay);
}



static int
dhdpcie_bus_download_state(dhd_bus_t *bus, bool enter)
{
	int bcmerror = 0;
	uint32 *cr4_regs;

	if (!bus->sih)
		return BCME_ERROR;
	/* To enter download state, disable ARM and reset SOCRAM.
	 * To exit download state, simply reset ARM (default is RAM boot).
	 */
	if (enter) {
		bus->alp_only = TRUE;

		/* some chips (e.g. 43602) have two ARM cores, the CR4 is receives the firmware. */
		cr4_regs = si_setcore(bus->sih, ARMCR4_CORE_ID, 0);

		if (cr4_regs == NULL && !(si_setcore(bus->sih, ARM7S_CORE_ID, 0)) &&
		    !(si_setcore(bus->sih, ARMCM3_CORE_ID, 0))) {
			DHD_ERROR(("%s: Failed to find ARM core!\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
			goto fail;
		}

		if (cr4_regs == NULL) { /* no CR4 present on chip */
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
			/* Halt ARM & remove reset */
			si_core_reset(bus->sih, SICF_CPUHALT, SICF_CPUHALT);
			if (bus->sih->chip == BCM43602_CHIP_ID) {
				W_REG(bus->pcie_mb_intr_osh, cr4_regs + ARMCR4REG_BANKIDX, 5);
				W_REG(bus->pcie_mb_intr_osh, cr4_regs + ARMCR4REG_BANKPDA, 0);
				W_REG(bus->pcie_mb_intr_osh, cr4_regs + ARMCR4REG_BANKIDX, 7);
				W_REG(bus->pcie_mb_intr_osh, cr4_regs + ARMCR4REG_BANKPDA, 0);
			}
			/* reset last 4 bytes of RAM address. to be used for shared area */
			dhdpcie_init_shared_addr(bus);
		}
	} else {
		if (!si_setcore(bus->sih, ARMCR4_CORE_ID, 0)) {
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
			if (bus->sih->chip == BCM43602_CHIP_ID) {
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

			/* write vars */
			if ((bcmerror = dhdpcie_bus_write_vars(bus))) {
				DHD_ERROR(("%s: could not write vars to RAM\n", __FUNCTION__));
				goto fail;
			}


			/* switch back to arm core again */
			if (!(si_setcore(bus->sih, ARMCR4_CORE_ID, 0))) {
				DHD_ERROR(("%s: Failed to find ARM CR4 core!\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}

			/* write address 0 with reset instruction */
			bcmerror = dhdpcie_bus_membytes(bus, TRUE, 0,
				(uint8 *)&bus->resetinstr, sizeof(bus->resetinstr));

			/* now remove reset and halt and continue to run CR4 */
		}

		si_core_reset(bus->sih, 0, 0);

		/* Allow HT Clock now that the ARM is running. */
		bus->alp_only = FALSE;

		bus->dhd->busstate = DHD_BUS_LOAD;
	}

fail:
	/* Always return to PCIE core */
	si_setcore(bus->sih, PCIE2_CORE_ID, 0);

	return bcmerror;
}

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
		if (!nvram_ularray)
			return BCME_NOMEM;

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
}

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
err:
	return bcmerror;
}

/* Add bus dump output to a buffer */
void dhd_bus_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	uint16 flowid;
	flow_ring_node_t *flow_ring_node;

	dhd_prot_print_info(dhdp, strbuf);
	for (flowid = 0; flowid < dhdp->num_flow_rings; flowid++) {
		flow_ring_node = DHD_FLOW_RING(dhdp, flowid);
		if (flow_ring_node->active) {
			bcm_bprintf(strbuf, "Flow:%d IF %d Prio %d  Qlen %d ",
				flow_ring_node->flowid, flow_ring_node->flow_info.ifindex,
				flow_ring_node->flow_info.tid, flow_ring_node->queue.len);
			dhd_prot_print_flow_ring(dhdp, flow_ring_node->prot_info, strbuf);
		}
	}
}

static void
dhd_update_txflowrings(dhd_pub_t *dhd)
{
	dll_t *item, *next;
	flow_ring_node_t *flow_ring_node;
	struct dhd_bus *bus = dhd->bus;

	for (item = dll_head_p(&bus->const_flowring);
	         !dll_end(&bus->const_flowring, item); item = next) {
		next = dll_next_p(item);

		flow_ring_node = dhd_constlist_to_flowring(item);
		ASSERT(flow_ring_node->active);
		dhd_prot_update_txflowring(dhd, flow_ring_node->flowid, flow_ring_node->prot_info);
	}
}


/* Mailbox ringbell Function */
static void
dhd_bus_gen_devmb_intr(struct dhd_bus *bus)
{
	if ((bus->sih->buscorerev == 2) || (bus->sih->buscorerev == 6) ||
		(bus->sih->buscorerev == 4)) {
		DHD_ERROR(("mailbox communication not supported\n"));
		return;
	}
	if (bus->db1_for_mb)  {
		/* this is a pcie core register, not the config regsiter */
		DHD_INFO(("writing a mail box interrupt to the device, through doorbell 1\n"));
		si_corereg(bus->sih, bus->sih->buscoreidx, PCIH2D_DB1, ~0, 0x12345678);
	}
	else {
		DHD_INFO(("writing a mail box interrupt to the device, through config space\n"));
		dhdpcie_bus_cfg_write_dword(bus, PCISBMbx, 4, (1 << 0));
		dhdpcie_bus_cfg_write_dword(bus, PCISBMbx, 4, (1 << 0));
	}
}

/* doorbell ring Function */
void
dhd_bus_ringbell(struct dhd_bus *bus, uint32 value)
{
	if ((bus->sih->buscorerev == 2) || (bus->sih->buscorerev == 6) ||
		(bus->sih->buscorerev == 4)) {
		si_corereg(bus->sih, bus->sih->buscoreidx, PCIMailBoxInt, PCIE_INTB, PCIE_INTB);
	} else {
		/* this is a pcie core register, not the config regsiter */
		DHD_INFO(("writing a door bell to the device\n"));
		si_corereg(bus->sih, bus->sih->buscoreidx, PCIH2D_MailBox, ~0, 0x12345678);
	}
}

static void
dhd_bus_ringbell_fast(struct dhd_bus *bus, uint32 value)
{
	W_REG(bus->pcie_mb_intr_osh, bus->pcie_mb_intr_addr, value);
}

static void
dhd_bus_ringbell_oldpcie(struct dhd_bus *bus, uint32 value)
{
	uint32 w;
	w = (R_REG(bus->pcie_mb_intr_osh, bus->pcie_mb_intr_addr) & ~PCIE_INTB) | PCIE_INTB;
	W_REG(bus->pcie_mb_intr_osh, bus->pcie_mb_intr_addr, w);
}

dhd_mb_ring_t
dhd_bus_get_mbintr_fn(struct dhd_bus *bus)
{
	if ((bus->sih->buscorerev == 2) || (bus->sih->buscorerev == 6) ||
		(bus->sih->buscorerev == 4)) {
		bus->pcie_mb_intr_addr = si_corereg_addr(bus->sih, bus->sih->buscoreidx,
			PCIMailBoxInt);
		if (bus->pcie_mb_intr_addr) {
			bus->pcie_mb_intr_osh = si_osh(bus->sih);
			return dhd_bus_ringbell_oldpcie;
		}
	} else {
		bus->pcie_mb_intr_addr = si_corereg_addr(bus->sih, bus->sih->buscoreidx,
			PCIH2D_MailBox);
		if (bus->pcie_mb_intr_addr) {
			bus->pcie_mb_intr_osh = si_osh(bus->sih);
			return dhd_bus_ringbell_fast;
		}
	}
	return dhd_bus_ringbell;
}

bool BCMFASTPATH
dhd_bus_dpc(struct dhd_bus *bus)
{
	uint32 intstatus = 0;
	uint32 newstatus = 0;
	bool resched = FALSE;	  /* Flag indicating resched wanted */

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus->dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s: Bus down, ret\n", __FUNCTION__));
		bus->intstatus = 0;
		return 0;
	}

	if (bus->dhd->busstate == DHD_BUS_SUSPEND) {
		resched = TRUE;
		DHD_ERROR(("%s : pcie is still in suspend state!!!\n", __FUNCTION__));
		OSL_DELAY(20 * 1000); /* 20ms */
		return resched;
	}

	intstatus = bus->intstatus;

	if ((bus->sih->buscorerev == 6) || (bus->sih->buscorerev == 4) ||
		(bus->sih->buscorerev == 2)) {
		newstatus =  dhdpcie_bus_cfg_read_dword(bus, PCIIntstatus, 4);
		dhdpcie_bus_cfg_write_dword(bus, PCIIntstatus, 4, newstatus);
		/* Merge new bits with previous */
		intstatus |= newstatus;
		bus->intstatus = 0;
		if (intstatus & I_MB) {
			dhdpcie_bus_process_mailbox_intr(bus, intstatus);
		}
	} else {
		/* this is a PCIE core register..not a config register... */
		newstatus = si_corereg(bus->sih, bus->sih->buscoreidx, PCIMailBoxInt, 0, 0);
		intstatus |= (newstatus & bus->def_intmask);
		si_corereg(bus->sih, bus->sih->buscoreidx, PCIMailBoxInt, intstatus, intstatus);
		if (intstatus & bus->def_intmask) {
			dhdpcie_bus_process_mailbox_intr(bus, intstatus);
			intstatus &= ~bus->def_intmask;
		}
	}

	dhdpcie_bus_intr_enable(bus);
	return resched;

}


static void
dhdpcie_send_mb_data(dhd_bus_t *bus, uint32 h2d_mb_data)
{
	uint32 cur_h2d_mb_data = 0;

	dhd_bus_cmn_readshared(bus, &cur_h2d_mb_data, HTOD_MB_DATA, 0);

	if (cur_h2d_mb_data != 0) {
		uint32 i = 0;
		DHD_INFO(("GRRRRRRR: MB transaction is already pending 0x%04x\n", cur_h2d_mb_data));
		while ((i++ < 100) && cur_h2d_mb_data) {
			OSL_DELAY(10);
			dhd_bus_cmn_readshared(bus, &cur_h2d_mb_data, HTOD_MB_DATA, 0);
		}
		if (i >= 100)
			DHD_ERROR(("waited 1ms for the dngl to ack the previous mb transaction\n"));
	}

	dhd_bus_cmn_writeshared(bus, &h2d_mb_data, sizeof(uint32), HTOD_MB_DATA, 0);
	dhd_bus_gen_devmb_intr(bus);

	if (h2d_mb_data == H2D_HOST_D3_INFORM)
		DHD_INFO_HW4(("%s: send H2D_HOST_D3_INFORM to dongle\n", __FUNCTION__));
}

static void
dhdpcie_handle_mb_data(dhd_bus_t *bus)
{
	uint32 d2h_mb_data = 0;
	uint32 zero = 0;
	dhd_bus_cmn_readshared(bus, &d2h_mb_data, DTOH_MB_DATA, 0);
	if (!d2h_mb_data)
		return;

	dhd_bus_cmn_writeshared(bus, &zero, sizeof(uint32), DTOH_MB_DATA, 0);

	DHD_INFO(("D2H_MB_DATA: 0x%04x\n", d2h_mb_data));
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
		DHD_INFO_HW4(("%s D2H_MB_DATA: Received D3 ACK\n", __FUNCTION__));
		if (!bus->wait_for_d3_ack) {
			bus->wait_for_d3_ack = 1;
			dhd_os_ioctl_resp_wake(bus->dhd);
		}
	}
	if (d2h_mb_data & D2H_DEV_FWHALT)  {
		DHD_INFO(("FW trap has happened\n"));
#ifdef DHD_DEBUG
		dhdpcie_checkdied(bus, NULL, 0);
#endif
		bus->dhd->busstate = DHD_BUS_DOWN;
	}
}

static void
dhdpcie_bus_process_mailbox_intr(dhd_bus_t *bus, uint32 intstatus)
{

	if ((bus->sih->buscorerev == 2) || (bus->sih->buscorerev == 6) ||
		(bus->sih->buscorerev == 4)) {
		/* Msg stream interrupt */
		if (intstatus & I_BIT1) {
			dhdpci_bus_read_frames(bus);
		} else if (intstatus & I_BIT0) {
			/* do nothing for Now */
		}
	}
	else {
		if (intstatus & (PCIE_MB_TOPCIE_FN0_0 | PCIE_MB_TOPCIE_FN0_1))
			dhdpcie_handle_mb_data(bus);
		if (intstatus & PCIE_MB_D2H_MB_MASK) {
				dhdpci_bus_read_frames(bus);
		}
	}
}

/* Decode dongle to host message stream */
static void
dhdpci_bus_read_frames(dhd_bus_t *bus)
{
	/* There may be frames in both ctrl buf and data buf; check ctrl buf first */
	DHD_PERIM_LOCK(bus->dhd); /* Take the perimeter lock */

	dhd_prot_process_ctrlbuf(bus->dhd);

	/* update the flow ring cpls */
	dhd_update_txflowrings(bus->dhd);

	dhd_prot_process_msgbuf_txcpl(bus->dhd);

	dhd_prot_process_msgbuf_rxcpl(bus->dhd);

	DHD_PERIM_UNLOCK(bus->dhd); /* Release the perimeter lock */
}

static int
dhdpcie_readshared(dhd_bus_t *bus)
{
	uint32 addr = 0;
	int rv, w_init, r_init;
	uint32 shaddr = 0;
	pciedev_shared_t *sh = bus->pcie_sh;
	dhd_timeout_t tmo;

	shaddr = bus->dongle_ram_base + bus->ramsize - 4;
	/* start a timer for 5 seconds */
	dhd_timeout_start(&tmo, MAX_READ_TIMEOUT);

	while (((addr == 0) || (addr == bus->nvram_csm)) && !dhd_timeout_expired(&tmo)) {
		/* Read last word in memory to determine address of sdpcm_shared structure */
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
		DHD_ERROR(("PCIe shared addr read took %u usec "
			"before dongle is ready\n", tmo.elapsed));
	}

	/* Read hndrte_shared structure */
	if ((rv = dhdpcie_bus_membytes(bus, FALSE, addr, (uint8 *)sh,
		sizeof(pciedev_shared_t))) < 0) {
		DHD_ERROR(("Failed to read PCIe shared struct,"
			"size read %d < %d\n", rv, (int)sizeof(pciedev_shared_t)));
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
	/* load bus console address */

#ifdef DHD_DEBUG
	bus->console_addr = sh->console_addr;
#endif

	/* Read the dma rx offset */
	bus->dma_rxoffset = bus->pcie_sh->dma_rxoffset;
	dhd_prot_rx_dataoffset(bus->dhd, bus->dma_rxoffset);

	DHD_ERROR(("DMA RX offset from shared Area %d\n", bus->dma_rxoffset));

	if ((sh->flags & PCIE_SHARED_VERSION_MASK) > PCIE_SHARED_VERSION) {
		DHD_ERROR(("%s: pcie_shared version %d in dhd "
		           "is older than pciedev_shared version %d in dongle\n",
		           __FUNCTION__, PCIE_SHARED_VERSION,
		           sh->flags & PCIE_SHARED_VERSION_MASK));
		return BCME_ERROR;
	}
	if ((sh->flags & PCIE_SHARED_VERSION_MASK) >= 4) {
		if (sh->flags & PCIE_SHARED_TXPUSH_SPRT) {
#ifdef DHDTCPACK_SUPPRESS
			/* Do not use tcpack suppress as packets don't stay in queue */
			dhd_tcpack_suppress_set(bus->dhd, TCPACK_SUP_OFF);
#endif
			bus->txmode_push = TRUE;
		} else
			bus->txmode_push = FALSE;
	}
	DHD_ERROR(("bus->txmode_push is set to %d\n", bus->txmode_push));

	/* Does the FW support DMA'ing r/w indices */
	if (sh->flags & PCIE_SHARED_DMA_INDEX) {

		DHD_ERROR(("%s: Host support DMAing indices: H2D:%d - D2H:%d. FW supports it\n",
			__FUNCTION__,
			(DMA_INDX_ENAB(bus->dhd->dma_h2d_ring_upd_support) ? 1 : 0),
			(DMA_INDX_ENAB(bus->dhd->dma_d2h_ring_upd_support) ? 1 : 0)));

	} else if (DMA_INDX_ENAB(bus->dhd->dma_d2h_ring_upd_support) ||
	           DMA_INDX_ENAB(bus->dhd->dma_h2d_ring_upd_support)) {

#ifdef BCM_INDX_DMA
		DHD_ERROR(("%s: Incompatible FW. FW does not support DMAing indices\n",
			__FUNCTION__));
		return BCME_ERROR;
#endif
		DHD_ERROR(("%s: Host supports DMAing indices but FW does not\n",
			__FUNCTION__));
		bus->dhd->dma_d2h_ring_upd_support = FALSE;
		bus->dhd->dma_h2d_ring_upd_support = FALSE;
	}


	/* get ring_info, ring_state and mb data ptrs and store the addresses in bus structure */
	{
		ring_info_t  ring_info;

		if ((rv = dhdpcie_bus_membytes(bus, FALSE, sh->rings_info_ptr,
			(uint8 *)&ring_info, sizeof(ring_info_t))) < 0)
			return rv;

		bus->h2d_mb_data_ptr_addr = ltoh32(sh->h2d_mb_data_ptr);
		bus->d2h_mb_data_ptr_addr = ltoh32(sh->d2h_mb_data_ptr);


		bus->max_sub_queues = ltoh16(ring_info.max_sub_queues);

		/* If both FW and Host support DMA'ing indices, allocate memory and notify FW
		 * The max_sub_queues is read from FW initialized ring_info
		 */
		if (DMA_INDX_ENAB(bus->dhd->dma_h2d_ring_upd_support)) {
			w_init = dhd_prot_init_index_dma_block(bus->dhd,
				HOST_TO_DNGL_DMA_WRITEINDX_BUFFER,
				bus->max_sub_queues);
			r_init = dhd_prot_init_index_dma_block(bus->dhd,
				DNGL_TO_HOST_DMA_READINDX_BUFFER,
				BCMPCIE_D2H_COMMON_MSGRINGS);

			if ((w_init != BCME_OK) || (r_init != BCME_OK)) {
				DHD_ERROR(("%s: Failed to allocate memory for dma'ing h2d indices"
						"Host will use w/r indices in TCM\n",
						__FUNCTION__));
				bus->dhd->dma_h2d_ring_upd_support = FALSE;
			}
		}

		if (DMA_INDX_ENAB(bus->dhd->dma_d2h_ring_upd_support)) {
			w_init = dhd_prot_init_index_dma_block(bus->dhd,
				DNGL_TO_HOST_DMA_WRITEINDX_BUFFER,
				BCMPCIE_D2H_COMMON_MSGRINGS);
			r_init = dhd_prot_init_index_dma_block(bus->dhd,
				HOST_TO_DNGL_DMA_READINDX_BUFFER,
				bus->max_sub_queues);

			if ((w_init != BCME_OK) || (r_init != BCME_OK)) {
				DHD_ERROR(("%s: Failed to allocate memory for dma'ing d2h indices"
						"Host will use w/r indices in TCM\n",
						__FUNCTION__));
				bus->dhd->dma_d2h_ring_upd_support = FALSE;
			}
		}

		/* read ringmem and ringstate ptrs from shared area and store in host variables */
		dhd_fillup_ring_sharedptr_info(bus, &ring_info);

		bcm_print_bytes("ring_info_raw", (uchar *)&ring_info, sizeof(ring_info_t));
		DHD_INFO(("ring_info\n"));

		DHD_ERROR(("max H2D queues %d\n", ltoh16(ring_info.max_sub_queues)));

		DHD_INFO(("mail box address\n"));
		DHD_INFO(("h2d_mb_data_ptr_addr 0x%04x\n", bus->h2d_mb_data_ptr_addr));
		DHD_INFO(("d2h_mb_data_ptr_addr 0x%04x\n", bus->d2h_mb_data_ptr_addr));
	}
	return BCME_OK;
}
/* Read ring mem and ring state ptr info from shared are in TCM */
static void
dhd_fillup_ring_sharedptr_info(dhd_bus_t *bus, ring_info_t *ring_info)
{
	uint16 i = 0;
	uint16 j = 0;
	uint32 tcm_memloc;
	uint32	d2h_w_idx_ptr, d2h_r_idx_ptr, h2d_w_idx_ptr, h2d_r_idx_ptr;

	/* Ring mem ptr info */
	/* Alloated in the order
		H2D_MSGRING_CONTROL_SUBMIT              0
		H2D_MSGRING_RXPOST_SUBMIT               1
		D2H_MSGRING_CONTROL_COMPLETE            2
		D2H_MSGRING_TX_COMPLETE                 3
		D2H_MSGRING_RX_COMPLETE                 4
		TX_FLOW_RING				5
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

		/* Tx flow Ring */
		if (bus->txmode_push) {
			bus->ring_sh[i].ring_mem_addr = tcm_memloc;
			DHD_INFO(("TX ring ring id %d ring mem addr 0x%04x \n",
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
			h2d_w_idx_ptr = h2d_w_idx_ptr + sizeof(uint32);
			h2d_r_idx_ptr = h2d_r_idx_ptr + sizeof(uint32);

			DHD_INFO(("h2d w/r : idx %d write %x read %x \n", i,
				bus->ring_sh[i].ring_state_w, bus->ring_sh[i].ring_state_r));
		}
		/* Store d2h common ring write/read pointers */
		for (j = 0; j < BCMPCIE_D2H_COMMON_MSGRINGS; j++, i++) {
			bus->ring_sh[i].ring_state_w = d2h_w_idx_ptr;
			bus->ring_sh[i].ring_state_r = d2h_r_idx_ptr;

			/* update mem block */
			d2h_w_idx_ptr = d2h_w_idx_ptr + sizeof(uint32);
			d2h_r_idx_ptr = d2h_r_idx_ptr + sizeof(uint32);

			DHD_INFO(("d2h w/r : idx %d write %x read %x \n", i,
				bus->ring_sh[i].ring_state_w, bus->ring_sh[i].ring_state_r));
		}

		/* Store txflow ring write/read pointers */
		if (bus->txmode_push) {
			bus->ring_sh[i].ring_state_w = h2d_w_idx_ptr;
			bus->ring_sh[i].ring_state_r = h2d_r_idx_ptr;

			DHD_INFO(("txflow : idx %d write %x read %x \n", i,
				bus->ring_sh[i].ring_state_w, bus->ring_sh[i].ring_state_r));
		} else {
			for (j = 0; j < (bus->max_sub_queues - BCMPCIE_H2D_COMMON_MSGRINGS);
				i++, j++)
			{
				bus->ring_sh[i].ring_state_w = h2d_w_idx_ptr;
				bus->ring_sh[i].ring_state_r = h2d_r_idx_ptr;

				/* update mem block */
				h2d_w_idx_ptr = h2d_w_idx_ptr + sizeof(uint32);
				h2d_r_idx_ptr = h2d_r_idx_ptr + sizeof(uint32);

				DHD_INFO(("FLOW Rings h2d w/r : idx %d write %x read %x \n", i,
					bus->ring_sh[i].ring_state_w,
					bus->ring_sh[i].ring_state_r));
			}
		}
	}
}
/* Initialize bus module: prepare for communication w/dongle */
int dhd_bus_init(dhd_pub_t *dhdp, bool enforce_mutex)
{
	dhd_bus_t *bus = dhdp->bus;
	int  ret = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(bus->dhd);
	if (!bus->dhd)
		return 0;

	/* Make sure we're talking to the core. */
	bus->reg = si_setcore(bus->sih, PCIE2_CORE_ID, 0);
	ASSERT(bus->reg != NULL);

	/* before opening up bus for data transfer, check if shared are is intact */
	ret = dhdpcie_readshared(bus);
	if (ret < 0) {
		DHD_ERROR(("%s :Shared area read failed \n", __FUNCTION__));
		return ret;
	}


	/* Make sure we're talking to the core. */
	bus->reg = si_setcore(bus->sih, PCIE2_CORE_ID, 0);
	ASSERT(bus->reg != NULL);

	/* Set bus state according to enable result */
	dhdp->busstate = DHD_BUS_DATA;

	/* Enable the interrupt after device is up */
	dhdpcie_bus_intr_enable(bus);

	/* bcmsdh_intr_unmask(bus->sdh); */

	return ret;

}


static void
dhdpcie_init_shared_addr(dhd_bus_t *bus)
{
	uint32 addr = 0;
	uint32 val = 0;
	addr = bus->dongle_ram_base + bus->ramsize - 4;
	dhdpcie_bus_membytes(bus, TRUE, addr, (uint8 *)&val, sizeof(val));
}


bool
dhdpcie_chipmatch(uint16 vendor, uint16 device)
{
	if (vendor != PCI_VENDOR_ID_BROADCOM) {
		DHD_ERROR(("%s: Unsupported vendor %x device %x\n", __FUNCTION__,
			vendor, device));
		return (-ENODEV);
	}

	if ((device == BCM4350_D11AC_ID) || (device == BCM4350_D11AC2G_ID) ||
		(device == BCM4350_D11AC5G_ID) || BCM4350_CHIP(device))
		return 0;

	if ((device == BCM4354_D11AC_ID) || (device == BCM4354_D11AC2G_ID) ||
		(device == BCM4354_D11AC5G_ID) || (device == BCM4354_CHIP_ID))
		return 0;

	if ((device == BCM4356_D11AC_ID) || (device == BCM4356_D11AC2G_ID) ||
		(device == BCM4356_D11AC5G_ID) || (device == BCM4356_CHIP_ID))
		return 0;

	if ((device == BCM4345_D11AC_ID) || (device == BCM4345_D11AC2G_ID) ||
		(device == BCM4345_D11AC5G_ID) || (device == BCM4345_CHIP_ID))
		return 0;

	if ((device == BCM4335_D11AC_ID) || (device == BCM4335_D11AC2G_ID) ||
		(device == BCM4335_D11AC5G_ID) || (device == BCM4335_CHIP_ID))
		return 0;

	if ((device == BCM43602_D11AC_ID) || (device == BCM43602_D11AC2G_ID) ||
		(device == BCM43602_D11AC5G_ID) || (device == BCM43602_CHIP_ID))
		return 0;

	if ((device == BCM43569_D11AC_ID) || (device == BCM43569_D11AC2G_ID) ||
		(device == BCM43569_D11AC5G_ID) || (device == BCM43569_CHIP_ID))
		return 0;

	if ((device == BCM4358_D11AC_ID) || (device == BCM4358_D11AC2G_ID) ||
		(device == BCM4358_D11AC5G_ID) || (device == BCM4358_CHIP_ID))
		return 0;

	if ((device == BCM4349_D11AC_ID) || (device == BCM4349_D11AC2G_ID) ||
		(device == BCM4349_D11AC5G_ID) || (device == BCM4349_CHIP_ID))
		return 0;
	if ((device == BCM4355_D11AC_ID) || (device == BCM4355_D11AC2G_ID) ||
		(device == BCM4355_D11AC5G_ID) || (device == BCM4355_CHIP_ID))
		return 0;
	if ((device == BCM4359_D11AC_ID) || (device == BCM4359_D11AC2G_ID) ||
		(device == BCM4359_D11AC5G_ID) || (device == BCM4359_CHIP_ID))
		return 0;


	DHD_ERROR(("%s: Unsupported vendor %x device %x\n", __FUNCTION__, vendor, device));
	return (-ENODEV);
}


/*

Name:  dhdpcie_cc_nvmshadow

Description:
A shadow of OTP/SPROM exists in ChipCommon Region
betw. 0x800 and 0xBFF (Backplane Addr. 0x1800_0800 and 0x1800_0BFF).
Strapping option (SPROM vs. OTP), presence of OTP/SPROM and its size
can also be read from ChipCommon Registers.
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
	chipc_corerev = si_corerev(bus->sih);

	/* Check ChipcommonCore Rev */
	if (chipc_corerev < 44) {
		DHD_ERROR(("%s: ChipcommonCore Rev %d < 44\n", __FUNCTION__, chipc_corerev));
		return BCME_UNSUPPORTED;
	}

	/* Check ChipID */
	if (((uint16)bus->sih->chip != BCM4350_CHIP_ID) &&
		((uint16)bus->sih->chip != BCM4345_CHIP_ID)) {
		DHD_ERROR(("%s: cc_nvmdump cmd. supported for 4350/4345 only\n",
			__FUNCTION__));
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
			otp_size =  (((chipcregs->capabilities & CC_CAP_OTPSIZE)
				        >> CC_CAP_OTPSIZE_SHIFT) + 1) * 1024;
			bcm_bprintf(b, "(Size %d bits)\n", otp_size);
		} else {
			/* This part is untested since newer chips have 40nm OTP */
			otp_size = otp_size_65nm[(chipcregs->capabilities & CC_CAP_OTPSIZE)
				        >> CC_CAP_OTPSIZE_SHIFT];
			bcm_bprintf(b, "(Size %d bits)\n", otp_size);
			DHD_INFO(("%s: 65nm/130nm OTP Size not tested. \n",
				__FUNCTION__));
		}
	}

	if (((chipcregs->sromcontrol & SRC_PRESENT) == 0) &&
		((chipcregs->capabilities & CC_CAP_OTPSIZE) == 0)) {
		DHD_ERROR(("%s: SPROM and OTP could not be found \n",
			__FUNCTION__));
		return BCME_NOTFOUND;
	}

	/* Check the strapping option in SpromCtrl: Set = OTP otherwise SPROM */
	if ((chipcregs->sromcontrol & SRC_OTPSEL) &&
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
	}
	else {
		DHD_ERROR(("%s: NVM Shadow does not exist in ChipCommon\n",
			__FUNCTION__));
		return BCME_NOTFOUND;
	}

	if (bus->regs == NULL) {
		DHD_ERROR(("ChipCommon Regs. not initialized\n"));
		return BCME_NOTREADY;
	} else {
	    bcm_bprintf(b, "\n OffSet:");

	    /* Point to the SPROM/OTP shadow in ChipCommon */
	    nvm_shadow = chipcregs->sromotp;

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
}


uint8 BCMFASTPATH
dhd_bus_is_txmode_push(dhd_bus_t *bus)
{
	return bus->txmode_push;
}

void dhd_bus_clean_flow_ring(dhd_bus_t *bus, uint16 flowid)
{
	void *pkt;
	flow_queue_t *queue;
	flow_ring_node_t *flow_ring_node;
	unsigned long flags;

	flow_ring_node = DHD_FLOW_RING(bus->dhd, flowid);
	ASSERT(flow_ring_node->flowid == flowid);

	queue = &flow_ring_node->queue;

	/* Call Flow ring clean up */
	dhd_prot_clean_flow_ring(bus->dhd, flow_ring_node->prot_info);
	dhd_flowid_free(bus->dhd, flow_ring_node->flow_info.ifindex,
	                flow_ring_node->flowid);

	/* clean up BUS level info */
	DHD_QUEUE_LOCK(queue->lock, flags);

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
	ASSERT(flow_queue_empty(queue));

	DHD_QUEUE_UNLOCK(queue->lock, flags);

	flow_ring_node->active = FALSE;

	dll_delete(&flow_ring_node->list);
}

/*
 * Allocate a Flow ring buffer,
 * Init Ring buffer,
 * Send Msg to device about flow ring creation
*/
int
dhd_bus_flow_ring_create_request(dhd_bus_t *bus, void *arg)
{
	flow_ring_node_t *flow_ring_node = (flow_ring_node_t *)arg;

	DHD_INFO(("%s :Flow create\n", __FUNCTION__));

	/* Send Msg to device about flow ring creation */
	dhd_prot_flow_ring_create(bus->dhd, flow_ring_node);

	flow_ring_node->status = FLOW_RING_STATUS_PENDING;

	dll_prepend(&bus->const_flowring, &flow_ring_node->list);

	return BCME_OK;
}

void
dhd_bus_flow_ring_create_response(dhd_bus_t *bus, uint16 flowid, int32 status)
{
	flow_ring_node_t *flow_ring_node;

	DHD_INFO(("%s :Flow Response %d \n", __FUNCTION__, flowid));

	flow_ring_node = DHD_FLOW_RING(bus->dhd, flowid);
	ASSERT(flow_ring_node->flowid == flowid);

	if (status != BCME_OK) {
		DHD_ERROR(("%s Flow create Response failure error status = %d \n",
		     __FUNCTION__, status));
		/* Call Flow clean up */
		dhd_bus_clean_flow_ring(bus, flowid);
		return;
	}

	flow_ring_node->status = FLOW_RING_STATUS_OPEN;

	dhd_bus_schedule_queue(bus, flowid, FALSE);

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

	if (flow_ring_node->status & FLOW_RING_STATUS_DELETE_PENDING) {
		DHD_ERROR(("%s :Delete Pending\n", __FUNCTION__));
		return BCME_ERROR;
	}

	queue = &flow_ring_node->queue; /* queue associated with flow ring */

	DHD_QUEUE_LOCK(queue->lock, flags);

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
	ASSERT(flow_queue_empty(queue));

	DHD_QUEUE_UNLOCK(queue->lock, flags);

	/* Send Msg to device about flow ring deletion */
	dhd_prot_flow_ring_delete(bus->dhd, flow_ring_node);

	flow_ring_node->status = FLOW_RING_STATUS_DELETE_PENDING;
	return BCME_OK;
}

void
dhd_bus_flow_ring_delete_response(dhd_bus_t *bus, uint16 flowid, uint32 status)
{
	flow_ring_node_t *flow_ring_node;

	DHD_INFO(("%s :Flow Delete Response %d \n", __FUNCTION__, flowid));

	flow_ring_node = DHD_FLOW_RING(bus->dhd, flowid);
	ASSERT(flow_ring_node->flowid == flowid);

	if (status != BCME_OK) {
		DHD_ERROR(("%s Flow Delete Response failure error status = %d \n",
		    __FUNCTION__, status));
		return;
	}
	/* Call Flow clean up */
	dhd_bus_clean_flow_ring(bus, flowid);

	flow_ring_node->status = FLOW_RING_STATUS_OPEN;
	flow_ring_node->active = FALSE;
	return;

}

int dhd_bus_flow_ring_flush_request(dhd_bus_t *bus, void *arg)
{
	void *pkt;
	flow_queue_t *queue;
	flow_ring_node_t *flow_ring_node;
	unsigned long flags;

	DHD_INFO(("%s :Flow Delete\n", __FUNCTION__));

	flow_ring_node = (flow_ring_node_t *)arg;
	queue = &flow_ring_node->queue; /* queue associated with flow ring */

	DHD_QUEUE_LOCK(queue->lock, flags);

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
	ASSERT(flow_queue_empty(queue));

	DHD_QUEUE_UNLOCK(queue->lock, flags);

	/* Send Msg to device about flow ring flush */
	dhd_prot_flow_ring_flush(bus->dhd, flow_ring_node);

	flow_ring_node->status = FLOW_RING_STATUS_FLUSH_PENDING;
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

	flow_ring_node = DHD_FLOW_RING(bus->dhd, flowid);
	ASSERT(flow_ring_node->flowid == flowid);

	flow_ring_node->status = FLOW_RING_STATUS_OPEN;
	return;
}

uint32
dhd_bus_max_h2d_queues(struct dhd_bus *bus, uint8 *txpush)
{
	if (bus->txmode_push)
		*txpush = 1;
	else
		*txpush = 0;
	return bus->max_sub_queues;
}

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

bool
dhdpcie_bus_dongle_attach(struct dhd_bus *bus)
{
	return dhdpcie_dongle_attach(bus);
}

int
dhd_bus_release_dongle(struct dhd_bus *bus)
{
	bool dongle_isolation;
	osl_t		*osh;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus) {
		osh = bus->osh;
		ASSERT(osh);

		if (bus->dhd) {
			dongle_isolation = bus->dhd->dongle_isolation;
			dhdpcie_bus_release_dongle(bus, osh, dongle_isolation, TRUE);
		}
	}

	return 0;
}
