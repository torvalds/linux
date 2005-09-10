/*
 * ip22-hpc.c: Routines for generic manipulation of the HPC controllers.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1998 Ralf Baechle
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/sgi/hpc3.h>
#include <asm/sgi/ioc.h>
#include <asm/sgi/ip22.h>

struct hpc3_regs *hpc3c0, *hpc3c1;

EXPORT_SYMBOL(hpc3c0);
EXPORT_SYMBOL(hpc3c1);

struct sgioc_regs *sgioc;

EXPORT_SYMBOL(sgioc);

/* We need software copies of these because they are write only. */
u8 sgi_ioc_reset, sgi_ioc_write;

extern char *system_type;

void __init sgihpc_init(void)
{
	/* ioremap can't fail */
	hpc3c0 = (struct hpc3_regs *)
		 ioremap(HPC3_CHIP0_BASE, sizeof(struct hpc3_regs));
	hpc3c1 = (struct hpc3_regs *)
		 ioremap(HPC3_CHIP1_BASE, sizeof(struct hpc3_regs));
	/* IOC lives in PBUS PIO channel 6 */
	sgioc = (struct sgioc_regs *)hpc3c0->pbus_extregs[6];

	hpc3c0->pbus_piocfg[6][0] |= HPC3_PIOCFG_DS16;
	if (ip22_is_fullhouse()) {
		/* Full House comes with INT2 which lives in PBUS PIO
		 * channel 4 */
		sgint = (struct sgint_regs *)hpc3c0->pbus_extregs[4];
		system_type = "SGI Indigo2";
	} else {
		/* Guiness comes with INT3 which is part of IOC */
		sgint = &sgioc->int3;
		system_type = "SGI Indy";
	}

	sgi_ioc_reset = (SGIOC_RESET_PPORT | SGIOC_RESET_KBDMOUSE |
			 SGIOC_RESET_EISA | SGIOC_RESET_ISDN |
			 SGIOC_RESET_LC0OFF);

	sgi_ioc_write = (SGIOC_WRITE_EASEL | SGIOC_WRITE_NTHRESH |
			 SGIOC_WRITE_TPSPEED | SGIOC_WRITE_EPSEL |
			 SGIOC_WRITE_U0AMODE | SGIOC_WRITE_U1AMODE);

	sgioc->reset = sgi_ioc_reset;
	sgioc->write = sgi_ioc_write;
}
