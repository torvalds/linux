/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef INCLUDE_PCI_GENERAL_H_
#define INCLUDE_PCI_GENERAL_H_

/* PCI CONFIGURATION SPACE */
#define mmPCI_CONFIG_ELBI_ADDR		0xFF0
#define mmPCI_CONFIG_ELBI_DATA		0xFF4
#define mmPCI_CONFIG_ELBI_CTRL		0xFF8
#define PCI_CONFIG_ELBI_CTRL_WRITE	(1 << 31)

#define mmPCI_CONFIG_ELBI_STS		0xFFC
#define PCI_CONFIG_ELBI_STS_ERR		(1 << 30)
#define PCI_CONFIG_ELBI_STS_DONE	(1 << 31)
#define PCI_CONFIG_ELBI_STS_MASK	(PCI_CONFIG_ELBI_STS_ERR | \
					PCI_CONFIG_ELBI_STS_DONE)

enum hl_revision_id {
	/* PCI revision ID 0 is not legal */
	REV_ID_INVALID				= 0x00,
	REV_ID_A				= 0x01,
	REV_ID_B				= 0x02,
	REV_ID_C				= 0x03,
	REV_ID_D				= 0x04
};

#endif /* INCLUDE_PCI_GENERAL_H_ */
