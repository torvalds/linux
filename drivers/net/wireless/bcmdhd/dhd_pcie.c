/*
 * DHD Bus Module for PCIE
 *
 * Copyright (C) 1999-2014, Broadcom Corporation
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
 * $Id: dhd_pcie.c 475815 2014-05-07 00:27:31Z $
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
#include <hndrte_armtrap.h>
#include <hndrte_cons.h>
#endif /* defined(DHD_DEBUG) */
#include <dngl_stats.h>
#include <pcie_core.h>
#include <dhd.h>
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <dhdioctl.h>
#include <sdiovar.h>
#include <bcmmsgbuf.h>
#include <pcicfg.h>
#include <circularbuf.h>
#include <dhd_pcie.h>
#include <bcmpcie.h>

#define MEMBLOCK	2048		/* Block size used for downloading of dongle image */
#define MAX_NVRAMBUF_SIZE	4096	/* max nvram buf size */

#define ARMCR4REG_BANKIDX	(0x40/sizeof(uint32))
#define ARMCR4REG_BANKPDA	(0x4C/sizeof(uint32))

int dhd_dongle_memsize;
int dhd_dongle_ramsize;
#ifdef DHD_DEBUG
static int dhdpcie_bus_readconsole(dhd_bus_t *bus);
#endif
static int dhdpcie_bus_membytes(dhd_bus_t *bus, bool write, ulong address, uint8 *data, uint size);
static int dhdpcie_bus_doiovar(dhd_bus_t *bus, const bcm_iovar_t *vi, uint32 actionid,
	const char *name, void *params,
	int plen, void *arg, int len, int val_size);
static int dhdpcie_bus_lpback_req(struct  dhd_bus *bus, uint32 intval);
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
static void dhdpcie_bus_wreg32(dhd_bus_t *bus, uint reg, uint32 data);
static uint32 dhdpcie_bus_rreg32(dhd_bus_t *bus, uint reg);
static void dhdpcie_bus_cfg_set_bar0_win(dhd_bus_t *bus, uint32 data);
static void dhdpcie_bus_reg_unmap(osl_t *osh, ulong addr, int size);
static int dhdpcie_cc_nvmshadow(dhd_bus_t *bus, struct bcmstrbuf *b);
static void dhdpcie_send_mb_data(dhd_bus_t *bus, uint32 h2d_mb_data);

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
	IOV_PCIEREG,
	IOV_PCIECFGREG,
	IOV_PCIECOREREG,
	IOV_SBREG,
	IOV_DONGLEISOLATION,
	IOV_LTRSLEEPON_UNLOOAD
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
	{"sbreg",	IOV_SBREG,	0,	IOVT_BUFFER,	sizeof(sdreg_t) },
	{"sleep_allowed",	IOV_SLEEP_ALLOWED,	0,	IOVT_BOOL,	0 },
	{"dngl_isolation", IOV_DONGLEISOLATION,	0,	IOVT_UINT32,	0 },
	{"ltrsleep_on_unload", IOV_LTRSLEEPON_UNLOOAD,	0,	IOVT_UINT32,	0 },
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

/** 'tcm' is the *host* virtual address at which tcm is mapped */
dhd_bus_t* dhdpcie_bus_attach(osl_t *osh, volatile char* regs, volatile char* tcm)
{
	dhd_bus_t *bus;

	int ret = 0;

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

		/* Attach to the OS network interface */
		DHD_TRACE(("%s(): Calling dhd_register_if() \n", __FUNCTION__));
		ret = dhd_register_if(bus->dhd, 0, TRUE);
		if (ret) {
			DHD_ERROR(("%s(): ERROR.. dhd_register_if() failed\n", __FUNCTION__));
			break;
		}
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


#ifdef DHD_ALLIRQ
			/* Lock here covers SMP */
			dhd_os_sdisrlock(bus->dhd);
#endif
			/* Count the interrupt call */
			bus->intrcount++;

			/* read interrupt status register!! Status bits will be cleared in DPC !! */
			bus->ipend = TRUE;
			dhdpcie_bus_intr_disable(bus); /* Disable interrupt!! */
			bus->intdis = TRUE;

#if defined(DHD_ALLIRQ) || defined(PCIE_ISR_THREAD)

			DHD_TRACE(("Calling dhd_bus_dpc() from %s\n", __FUNCTION__));
			DHD_OS_WAKE_LOCK(bus->dhd);
			while (dhd_bus_dpc(bus));
			DHD_OS_WAKE_UNLOCK(bus->dhd);
#else
			bus->dpc_sched = TRUE;
			dhd_sched_dpc(bus->dhd);     /* queue DPC now!! */
#endif /* defined(DHD_ALLIRQ) || defined(SDIO_ISR_THREAD) */

#ifdef DHD_ALLIRQ
			dhd_os_sdisrunlock(bus->dhd);
#endif
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

	dhdpcie_bus_wreg32(bus,  OFFSETOF(sbpcieregs_t, configaddr), 0x4e0);
	val = dhdpcie_bus_rreg32(bus,  OFFSETOF(sbpcieregs_t, configdata));
	dhdpcie_bus_wreg32(bus,  OFFSETOF(sbpcieregs_t, configdata), val);

	/* Get info on the ARM and SOCRAM cores... */
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
		case BCM4335_CHIP_ID:
			bus->dongle_ram_base = CR4_4335_RAM_BASE;
			break;
		case BCM4354_CHIP_ID:
		case BCM4350_CHIP_ID:
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

	if (bus) {

		if ((bus->sih->buscorerev == 2) || (bus->sih->buscorerev == 6) ||
			(bus->sih->buscorerev == 4)) {
			dhpcie_bus_mask_interrupt(bus);
		}
		else if (bus->sih) {
			si_corereg(bus->sih, bus->sih->buscoreidx, PCIMailBoxMask,
				bus->def_intmask, 0);
		}
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


		if (!dongle_isolation) {
			uint32 val, i;
			uint16 cfg_offset[] = {0x4, 0x4C, 0x58, 0x5C, 0x60, 0x64, 0xDC,
				0x228, 0x248,  0x4e0, 0x4f4};
			si_corereg(bus->sih, SI_CC_IDX, OFFSETOF(chipcregs_t, watchdog), ~0, 4);
			/* apply the WAR: need to restore the config space snoop bus values */
			OSL_DELAY(100000);

			for (i = 0; i < ARRAYSIZE(cfg_offset); i++) {
				dhdpcie_bus_wreg32(bus,  OFFSETOF(sbpcieregs_t, configaddr),
					cfg_offset[i]);
				val = dhdpcie_bus_rreg32(bus,
					OFFSETOF(sbpcieregs_t, configdata));
				DHD_INFO(("SNOOP_BUS_UPDATE: config offset 0x%04x, value 0x%04x\n",
					cfg_offset[i], val));
				dhdpcie_bus_wreg32(bus,  OFFSETOF(sbpcieregs_t, configdata), val);
			}
		}
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

/* 32 bit pio write to device TCM */
void
dhdpcie_bus_wreg32(dhd_bus_t *bus, uint reg, uint32 data)
{
	*(volatile uint32 *)(bus->regs + reg) = (uint32)data;

}

uint32
dhdpcie_bus_rreg32(dhd_bus_t *bus, uint reg)
{
	uint32 data;

	data = *(volatile uint32 *)(bus->regs + reg);
	return data;
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

	if (enforce_mutex)
		dhd_os_sdlock(bus->dhd);

	bus->dhd->busstate = DHD_BUS_DOWN;
	dhdpcie_bus_intr_disable(bus);
	status =  dhdpcie_bus_cfg_read_dword(bus, PCIIntstatus, 4);
	dhdpcie_bus_cfg_write_dword(bus, PCIIntstatus, 4, status);

	/* Clear rx control and wake any waiters */
	bus->rxlen = 0;
	dhd_os_ioctl_resp_wake(bus->dhd);

	if (enforce_mutex)
		dhd_os_sdunlock(bus->dhd);

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
		len = strlen(bus->nvram_params);
		ASSERT(len <= MAX_NVRAMBUF_SIZE);
		memcpy(memblock, bus->nvram_params, len);
	}
	if (len > 0 && len < MAX_NVRAMBUF_SIZE) {
		bufp = (char *)memblock;
		bufp[len] = 0;
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
	dhd_os_sdlock(bus->dhd);
	rxlen = bus->rxlen;
	bcopy(&bus->ioct_resp, msg, sizeof(ioct_resp_hdr_t));
	bus->rxlen = 0;
	dhd_os_sdunlock(bus->dhd);

	if (rxlen) {
		DHD_CTL(("%s: resumed on rxctl frame, got %d\n", __FUNCTION__, rxlen));
	} else if (timeleft == 0) {
		DHD_ERROR(("%s: resumed on timeout\n", __FUNCTION__));
		bus->ioct_resp.pkt_id = 0;
		bus->ioct_resp.status = 0xffff;
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
	addr = bus->console_addr + OFFSETOF(hndrte_cons_t, log);

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
	uint i = 0;
	/* In remap mode, adjust address beyond socram and redirect
	 * to devram at SOCDEVRAM_BP_ADDR since remap address > orig_ramsize
	 * is not backplane accessible
	 */


	/* Determine initial transfer parameters */
	dsize = sizeof(uint8);

	/* Do the transfer(s) */
	if (write) {
		while (size) {
			dhdpcie_bus_wtcm8(bus, address, *data);
			/* Adjust for next transfer (if any) */
			if ((size -= dsize)) {
				data += dsize;
				address += dsize;
			}
		}
	} else {
		while (size) {
			data[i] = dhdpcie_bus_rtcm8(bus, address);
			/* Adjust for next transfer (if any) */
			if ((size -= dsize)) {
				i++;
				address += dsize;
			}
		}
	}
	return bcmerror;
}

/* Send a data frame to the dongle.  Callee disposes of txp. */
int BCMFASTPATH
dhd_bus_txdata(struct dhd_bus *bus, void *txp, uint8 ifidx)
{
	return dhd_prot_txdata(bus->dhd, txp, ifidx);
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
dhd_bus_update_retlen(dhd_bus_t *bus, uint32 retlen, uint32 pkt_id, uint32 status,
	uint32 inline_data)
{
	bus->rxlen = retlen;
	bus->ioct_resp.pkt_id = pkt_id;
	bus->ioct_resp.status = status;
	bus->ioct_resp.inline_data = inline_data;
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

	/* Exclusive bus access */
	dhd_os_sdlock(bus->dhd);

	/* Don't allow input if dongle is in reset */
	if (bus->dhd->dongle_reset) {
		dhd_os_sdunlock(bus->dhd);
		return BCME_NOTREADY;
	}

	/* Zero cbuf_index */
	addr = bus->console_addr + OFFSETOF(hndrte_cons_t, cbuf_idx);
	val = htol32(0);
	if ((rv = dhdpcie_bus_membytes(bus, TRUE, addr, (uint8 *)&val, sizeof(val))) < 0)
		goto done;

	/* Write message into cbuf */
	addr = bus->console_addr + OFFSETOF(hndrte_cons_t, cbuf);
	if ((rv = dhdpcie_bus_membytes(bus, TRUE, addr, (uint8 *)msg, msglen)) < 0)
		goto done;

	/* Write length into vcons_in */
	addr = bus->console_addr + OFFSETOF(hndrte_cons_t, vcons_in);
	val = htol32(msglen);
	if ((rv = dhdpcie_bus_membytes(bus, TRUE, addr, (uint8 *)&val, sizeof(val))) < 0)
		goto done;

	dhd_post_dummy_msg(bus->dhd);
done:

	dhd_os_sdunlock(bus->dhd);

	return rv;
}
#endif /* defined(DHD_DEBUG) */

/* Process rx frame , Send up the layer to netif */
void
dhd_bus_rx_frame(struct dhd_bus *bus, void* pkt, int ifidx, uint pkt_count)
{
	dhd_os_sdunlock(bus->dhd);
	dhd_rx_frame(bus->dhd, ifidx, pkt, pkt_count, 0);
	dhd_os_sdlock(bus->dhd);
}

/** 'offset' is a backplane address */
void
dhdpcie_bus_wtcm8(dhd_bus_t *bus, ulong offset, uint8 data)
{
	*(volatile uint8 *)(bus->tcm + offset) = (uint8)data;
}

uint8
dhdpcie_bus_rtcm8(dhd_bus_t *bus, ulong offset)
{
	volatile uint8 data = *(volatile uint8 *)(bus->tcm + offset);
	return data;
}

void
dhdpcie_bus_wtcm32(dhd_bus_t *bus, ulong offset, uint32 data)
{
	*(volatile uint32 *)(bus->tcm + offset) = (uint32)data;
}
void
dhdpcie_bus_wtcm16(dhd_bus_t *bus, ulong offset, uint16 data)
{
	*(volatile uint16 *)(bus->tcm + offset) = (uint16)data;
}

uint16
dhdpcie_bus_rtcm16(dhd_bus_t *bus, ulong offset)
{
	volatile uint16 data = *(volatile uint16 *)(bus->tcm + offset);
	return data;
}

uint32
dhdpcie_bus_rtcm32(dhd_bus_t *bus, ulong offset)
{
	volatile uint32 data = *(volatile uint32 *)(bus->tcm + offset);
	return data;
}

void
dhd_bus_cmn_writeshared(dhd_bus_t *bus, void * data, uint32 len, uint8 type)
{
	uint64 long_data;
	ulong tcm_offset;

	DHD_INFO(("%s: writing to msgbuf type %d, len %d\n", __FUNCTION__, type, len));

	switch (type) {
		case DNGL_TO_HOST_BUF_ADDR :
			long_data = HTOL64(*(uint64 *)data);
			tcm_offset = bus->d2h_data_ring_mem_addr;
			tcm_offset += OFFSETOF(ring_mem_t, base_addr);
			dhdpcie_bus_membytes(bus, TRUE, tcm_offset, (uint8*) &long_data, len);
			prhex(__FUNCTION__, data, len);
			break;
		case HOST_TO_DNGL_BUF_ADDR :
			long_data = HTOL64(*(uint64 *)data);
			tcm_offset = bus->h2d_data_ring_mem_addr;
			tcm_offset += OFFSETOF(ring_mem_t, base_addr);
			dhdpcie_bus_membytes(bus, TRUE, tcm_offset, (uint8*) &long_data, len);
			prhex(__FUNCTION__, data, len);
			break;
		case HOST_TO_DNGL_WPTR :
			tcm_offset = bus->h2d_data_ring_state_addr;
			tcm_offset += OFFSETOF(ring_state_t, w_offset);
			dhdpcie_bus_wtcm32(bus, tcm_offset, (uint32) HTOL32(*(uint32 *)data));
			break;
		case DNGL_TO_HOST_RPTR :
			tcm_offset = bus->d2h_data_ring_state_addr;
			tcm_offset += OFFSETOF(ring_state_t, r_offset);
			dhdpcie_bus_wtcm16(bus, tcm_offset, (uint16) HTOL16(*(uint16 *)data));
			break;
		case HOST_TO_DNGL_CTRLBUF_ADDR:
			long_data = HTOL64(*(uint64 *)data);
			tcm_offset = bus->h2d_ctrl_ring_mem_addr;
			tcm_offset += OFFSETOF(ring_mem_t, base_addr);
			dhdpcie_bus_membytes(bus, TRUE, tcm_offset, (uint8 *) &long_data, len);
			break;
		case DNGL_TO_HOST_CTRLBUF_ADDR:
			long_data = HTOL64(*(uint64 *)data);
			tcm_offset = bus->d2h_ctrl_ring_mem_addr;
			tcm_offset += OFFSETOF(ring_mem_t, base_addr);
			dhdpcie_bus_membytes(bus, TRUE, tcm_offset, (uint8 *) &long_data, len);
			break;
		case HTOD_CTRL_WPTR:
			tcm_offset = bus->h2d_ctrl_ring_state_addr;
			tcm_offset += OFFSETOF(ring_state_t, w_offset);
			dhdpcie_bus_wtcm32(bus, tcm_offset, (uint32) HTOL32(*(uint32 *)data));
			break;
		case DTOH_CTRL_RPTR:
			tcm_offset = bus->d2h_ctrl_ring_state_addr;
			tcm_offset += OFFSETOF(ring_state_t, r_offset);
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
dhd_bus_cmn_readshared(dhd_bus_t *bus, void* data, uint8 type)
{
	pciedev_shared_t *sh;
	ulong tcm_offset;

	sh = (pciedev_shared_t*)bus->shared_addr;

	switch (type) {
		case HOST_TO_DNGL_RPTR :
			tcm_offset = bus->h2d_data_ring_state_addr;
			tcm_offset += OFFSETOF(ring_state_t, r_offset);
			*(uint16*)data = LTOH16(dhdpcie_bus_rtcm16(bus, tcm_offset));
			break;
		case DNGL_TO_HOST_WPTR :
			tcm_offset = bus->d2h_data_ring_state_addr;
			tcm_offset += OFFSETOF(ring_state_t, w_offset);
			*(uint32*)data = LTOH32(dhdpcie_bus_rtcm32(bus, tcm_offset));
			break;
		case TOTAL_LFRAG_PACKET_CNT :
			*(uint16*)data = LTOH16(dhdpcie_bus_rtcm16(bus,
				(ulong) &sh->total_lfrag_pkt_cnt));
			break;
		case HTOD_CTRL_RPTR:
			tcm_offset = bus->h2d_ctrl_ring_state_addr;
			tcm_offset += OFFSETOF(ring_state_t, r_offset);
			*(uint16*)data = LTOH16(dhdpcie_bus_rtcm16(bus, tcm_offset));
			break;
		case DTOH_CTRL_WPTR:
			tcm_offset = bus->d2h_ctrl_ring_state_addr;
			tcm_offset += OFFSETOF(ring_state_t, w_offset);
			*(uint32*)data = LTOH32(dhdpcie_bus_rtcm32(bus, tcm_offset));
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

	/* Some ioctls use the bus */
	dhd_os_sdlock(bus->dhd);

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

	default:
		bcmerror = BCME_UNSUPPORTED;
		break;
	}

exit:


	dhd_os_sdunlock(bus->dhd);

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
	/* this is a pcie core register, not the config regsiter */
	DHD_INFO(("writing a mail box interrupt to the device, through config space\n"));
	dhdpcie_bus_cfg_write_dword(bus, PCISBMbx, 4, (1 << 0));
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

#ifndef DHD_ALLIRQ
	dhd_os_sdlock(bus->dhd);
#endif /* DHD_ALLIRQ */
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
#ifndef DHD_ALLIRQ
	dhd_os_sdunlock(bus->dhd);
#endif /* DHD_ALLIRQ */
	return resched;

}


static void
dhdpcie_send_mb_data(dhd_bus_t *bus, uint32 h2d_mb_data)
{
	uint32 cur_h2d_mb_data = 0;

	dhd_bus_cmn_readshared(bus, &cur_h2d_mb_data, HTOD_MB_DATA);

	if (cur_h2d_mb_data != 0) {
		uint32 i = 0;
		DHD_INFO(("GRRRRRRR: MB transaction is already pending 0x%04x\n", cur_h2d_mb_data));
		while ((i++ < 100) && cur_h2d_mb_data) {
			OSL_DELAY(10);
			dhd_bus_cmn_readshared(bus, &cur_h2d_mb_data, HTOD_MB_DATA);
		}
		if (i >= 100)
			DHD_ERROR(("waited 1ms for the dngl to ack the previous mb transaction\n"));
	}

	dhd_bus_cmn_writeshared(bus, &h2d_mb_data, sizeof(uint32), HTOD_MB_DATA);
	dhd_bus_gen_devmb_intr(bus);
}

static void
dhdpcie_handle_mb_data(dhd_bus_t *bus)
{
	uint32 d2h_mb_data = 0;
	uint32 zero = 0;

	dhd_bus_cmn_readshared(bus, &d2h_mb_data, DTOH_MB_DATA);
	if (!d2h_mb_data)
		return;

	dhd_bus_cmn_writeshared(bus, &zero, sizeof(uint32), DTOH_MB_DATA);

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
		DHD_INFO(("D2H_MB_DATA: D3 ACK\n"));
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
		if (intstatus & PCIE_MB_D2H_MB_MASK)
			dhdpci_bus_read_frames(bus);
	}

}

/* Decode dongle to host message stream */
static void
dhdpci_bus_read_frames(dhd_bus_t *bus)
{
	/* There may be frames in both ctrl buf and data buf; check ctrl buf first */
	if (dhd_prot_dtohsplit(bus->dhd))
		dhd_prot_process_ctrlbuf(bus->dhd);
	dhd_prot_process_msgbuf(bus->dhd);
}

static int
dhdpcie_readshared(dhd_bus_t *bus)
{
	uint32 addr = 0;
	int rv;
	uint32 shaddr = 0;
	pciedev_shared_t *sh = bus->pcie_sh;
	dhd_timeout_t tmo;

	shaddr = bus->dongle_ram_base + bus->ramsize - 4;
	/* start a timer for 5 seconds */
	dhd_timeout_start(&tmo, MAX_READ_TIMEOUT);

	while (((addr == 0) || (addr == bus->nvram_csm)) && !dhd_timeout_expired(&tmo)) {
		/* Read last word in memory to determine address of sdpcm_shared structure */
		if ((rv = dhdpcie_bus_membytes(bus, FALSE, shaddr, (uint8 *)&addr, 4)) < 0)
			return rv;

		addr = ltoh32(addr);
	}

	if ((addr == 0) || (addr == bus->nvram_csm)) {
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
	/* get ring_info, ring_state and mb data ptrs and store the addresses in bus structure */
	{
		ring_info_t  ring_info;
		uint32 tcm_rmem_loc;
		uint32 tcm_rstate_loc;

		if ((rv = dhdpcie_bus_membytes(bus, FALSE, sh->rings_info_ptr,
			(uint8 *)&ring_info, sizeof(ring_info_t))) < 0)
			return rv;
		bus->h2d_ring_count = ring_info.h2d_ring_count;
		bus->d2h_ring_count = ring_info.d2h_ring_count;

		bus->h2d_mb_data_ptr_addr = ltoh32(sh->h2d_mb_data_ptr);
		bus->d2h_mb_data_ptr_addr = ltoh32(sh->d2h_mb_data_ptr);

		bus->ringmem_ptr = ltoh32(ring_info.ringmem_ptr);
		bus->ring_state_ptr = ltoh32(ring_info.ring_state_ptr);

		bcm_print_bytes("ring_info_raw", (uchar *)&ring_info, sizeof(ring_info_t));
		DHD_INFO(("ring_info\n"));
		DHD_INFO(("h2d_ring_count %d\n", bus->h2d_ring_count));
		DHD_INFO(("d2h_ring_count %d\n", bus->d2h_ring_count));
		DHD_INFO(("ringmem_ptr 0x%04x\n", bus->ringmem_ptr));
		DHD_INFO(("ringstate_ptr 0x%04x\n", bus->ring_state_ptr));

		tcm_rmem_loc = bus->ringmem_ptr;
		tcm_rstate_loc = bus->ring_state_ptr;

		if (bus->h2d_ring_count > 1) {
			bus->h2d_ctrl_ring_mem_addr = tcm_rmem_loc;
			tcm_rmem_loc += sizeof(ring_mem_t);
			bus->h2d_ctrl_ring_state_addr = tcm_rstate_loc;
			tcm_rstate_loc += sizeof(ring_state_t);
		}
		bus->h2d_data_ring_mem_addr = tcm_rmem_loc;
		tcm_rmem_loc += sizeof(ring_mem_t);
		bus->h2d_data_ring_state_addr = tcm_rstate_loc;
		tcm_rstate_loc += sizeof(ring_state_t);

		if (bus->d2h_ring_count > 1) {
			bus->d2h_ctrl_ring_mem_addr = tcm_rmem_loc;
			tcm_rmem_loc += sizeof(ring_mem_t);
			bus->d2h_ctrl_ring_state_addr = tcm_rstate_loc;
			tcm_rstate_loc += sizeof(ring_state_t);
		}
		bus->d2h_data_ring_mem_addr = tcm_rmem_loc;
		bus->d2h_data_ring_state_addr = tcm_rstate_loc;

		DHD_INFO(("ring_mem\n"));
		DHD_INFO(("h2d_data_ring_mem 0x%04x\n", bus->h2d_data_ring_mem_addr));
		DHD_INFO(("h2d_ctrl_ring_mem 0x%04x\n", bus->h2d_ctrl_ring_mem_addr));
		DHD_INFO(("d2h_data_ring_mem 0x%04x\n", bus->d2h_data_ring_mem_addr));
		DHD_INFO(("d2h_ctrl_ring_mem 0x%04x\n", bus->d2h_ctrl_ring_mem_addr));

		DHD_INFO(("ring_state\n"));
		DHD_INFO(("h2d_data_ring_state 0x%04x\n", bus->h2d_data_ring_state_addr));
		DHD_INFO(("h2d_ctrl_ring_state 0x%04x\n", bus->h2d_ctrl_ring_state_addr));
		DHD_INFO(("d2h_data_ring_state 0x%04x\n", bus->d2h_data_ring_state_addr));
		DHD_INFO(("d2h_ctrl_ring_state 0x%04x\n", bus->d2h_ctrl_ring_state_addr));

		DHD_INFO(("mail box address\n"));
		DHD_INFO(("h2d_mb_data_ptr_addr 0x%04x\n", bus->h2d_mb_data_ptr_addr));
		DHD_INFO(("d2h_mb_data_ptr_addr 0x%04x\n", bus->d2h_mb_data_ptr_addr));
	}
	return BCME_OK;
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

	if (enforce_mutex)
		dhd_os_sdlock(bus->dhd);

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

	if (enforce_mutex)
		dhd_os_sdunlock(bus->dhd);

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

	if ((device == BCM4345_D11AC_ID) || (device == BCM4345_D11AC2G_ID) ||
		(device == BCM4345_D11AC5G_ID) || (device == BCM4345_CHIP_ID))
		return 0;

	if ((device == BCM4335_D11AC_ID) || (device == BCM4335_D11AC2G_ID) ||
		(device == BCM4335_D11AC5G_ID) || (device == BCM4335_CHIP_ID))
		return 0;

	if ((device == BCM43602_D11AC_ID) || (device == BCM43602_D11AC2G_ID) ||
		(device == BCM43602_D11AC5G_ID) || (device == BCM43602_CHIP_ID))
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
