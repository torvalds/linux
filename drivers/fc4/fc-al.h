/* fc-al.h: Definitions for Fibre Channel Arbitrated Loop topology.
 *
 * Copyright (C) 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * Sources:
 *	Fibre Channel Arbitrated Loop (FC-AL), ANSI, Rev. 4.5, 1995
 */

#ifndef __FC_AL_H
#define __FC_AL_H

/* Loop initialization payloads */
#define	FC_AL_LISM	0x11010000	/* Select Master, 12B payload */
#define FC_AL_LIFA	0x11020000	/* Fabric Assign AL_PA bitmap, 20B payload */
#define FC_AL_LIPA	0x11030000	/* Previously Acquired AL_PA bitmap, 20B payload */
#define FC_AL_LIHA	0x11040000	/* Hard Assigned AL_PA bitmap, 20B payload */
#define FC_AL_LISA	0x11050000	/* Soft Assigned AL_PA bitmap, 20B payload */
#define FC_AL_LIRP	0x11060000	/* Report AL_PA position map, 132B payload */
#define FC_AL_LILP	0x11070000	/* Loop AL_PA position map, 132B payload */

typedef struct {
	u32	magic;
	u8	len;
	u8	alpa[127];
} fc_al_posmap;

#endif /* !(__FC_H) */
