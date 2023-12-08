/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

*/

#ifndef __MANTIS_IOC_H
#define __MANTIS_IOC_H

#define GPIF_A00		0x00
#define GPIF_A01		0x01
#define GPIF_A02		0x02
#define GPIF_A03		0x03
#define GPIF_A04		0x04
#define GPIF_A05		0x05
#define GPIF_A06		0x06
#define GPIF_A07		0x07
#define GPIF_A08		0x08
#define GPIF_A09		0x09
#define GPIF_A10		0x0a
#define GPIF_A11		0x0b

#define GPIF_A12		0x0c
#define GPIF_A13		0x0d
#define GPIF_A14		0x0e

enum mantis_stream_control {
	STREAM_TO_HIF = 0,
	STREAM_TO_CAM
};

extern int mantis_get_mac(struct mantis_pci *mantis);
extern void mantis_gpio_set_bits(struct mantis_pci *mantis, u32 bitpos, u8 value);

extern int mantis_stream_control(struct mantis_pci *mantis, enum mantis_stream_control stream_ctl);

#endif /* __MANTIS_IOC_H */
