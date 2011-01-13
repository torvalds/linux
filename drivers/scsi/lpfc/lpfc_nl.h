/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2010 Emulex.  All rights reserved.                *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

/* Event definitions for RegisterForEvent */
#define FC_REG_LINK_EVENT		0x0001	/* link up / down events */
#define FC_REG_RSCN_EVENT		0x0002	/* RSCN events */
#define FC_REG_CT_EVENT			0x0004	/* CT request events */
#define FC_REG_DUMP_EVENT		0x0010	/* Dump events */
#define FC_REG_TEMPERATURE_EVENT	0x0020	/* temperature events */
#define FC_REG_VPORTRSCN_EVENT		0x0040	/* Vport RSCN events */
#define FC_REG_ELS_EVENT		0x0080	/* lpfc els events */
#define FC_REG_FABRIC_EVENT		0x0100	/* lpfc fabric events */
#define FC_REG_SCSI_EVENT		0x0200	/* lpfc scsi events */
#define FC_REG_BOARD_EVENT		0x0400	/* lpfc board events */
#define FC_REG_ADAPTER_EVENT		0x0800	/* lpfc adapter events */
#define FC_REG_EVENT_MASK		(FC_REG_LINK_EVENT | \
						FC_REG_RSCN_EVENT | \
						FC_REG_CT_EVENT | \
						FC_REG_DUMP_EVENT | \
						FC_REG_TEMPERATURE_EVENT | \
						FC_REG_VPORTRSCN_EVENT | \
						FC_REG_ELS_EVENT | \
						FC_REG_FABRIC_EVENT | \
						FC_REG_SCSI_EVENT | \
						FC_REG_BOARD_EVENT | \
						FC_REG_ADAPTER_EVENT)
/* Temperature events */
#define LPFC_CRIT_TEMP		0x1
#define LPFC_THRESHOLD_TEMP	0x2
#define LPFC_NORMAL_TEMP	0x3
/*
 * All net link event payloads will begin with and event type
 * and subcategory. The event type must come first.
 * The subcategory further defines the data that follows in the rest
 * of the payload. Each category will have its own unique header plus
 * any addtional data unique to the subcategory.
 * The payload sent via the fc transport is one-way driver->application.
 */

/* RSCN event header */
struct lpfc_rscn_event_header {
	uint32_t event_type;
	uint32_t payload_length; /* RSCN data length in bytes */
	uint32_t rscn_payload[];
};

/* els event header */
struct lpfc_els_event_header {
	uint32_t event_type;
	uint32_t subcategory;
	uint8_t wwpn[8];
	uint8_t wwnn[8];
};

/* subcategory codes for FC_REG_ELS_EVENT */
#define LPFC_EVENT_PLOGI_RCV		0x01
#define LPFC_EVENT_PRLO_RCV		0x02
#define LPFC_EVENT_ADISC_RCV		0x04
#define LPFC_EVENT_LSRJT_RCV		0x08
#define LPFC_EVENT_LOGO_RCV		0x10

/* special els lsrjt event */
struct lpfc_lsrjt_event {
	struct lpfc_els_event_header header;
	uint32_t command;
	uint32_t reason_code;
	uint32_t explanation;
};

/* special els logo event */
struct lpfc_logo_event {
	struct lpfc_els_event_header header;
	uint8_t logo_wwpn[8];
};

/* fabric event header */
struct lpfc_fabric_event_header {
	uint32_t event_type;
	uint32_t subcategory;
	uint8_t wwpn[8];
	uint8_t wwnn[8];
};

/* subcategory codes for FC_REG_FABRIC_EVENT */
#define LPFC_EVENT_FABRIC_BUSY		0x01
#define LPFC_EVENT_PORT_BUSY		0x02
#define LPFC_EVENT_FCPRDCHKERR		0x04

/* special case fabric fcprdchkerr event */
struct lpfc_fcprdchkerr_event {
	struct lpfc_fabric_event_header header;
	uint32_t lun;
	uint32_t opcode;
	uint32_t fcpiparam;
};


/* scsi event header */
struct lpfc_scsi_event_header {
	uint32_t event_type;
	uint32_t subcategory;
	uint32_t lun;
	uint8_t wwpn[8];
	uint8_t wwnn[8];
};

/* subcategory codes for FC_REG_SCSI_EVENT */
#define LPFC_EVENT_QFULL	0x0001
#define LPFC_EVENT_DEVBSY	0x0002
#define LPFC_EVENT_CHECK_COND	0x0004
#define LPFC_EVENT_LUNRESET	0x0008
#define LPFC_EVENT_TGTRESET	0x0010
#define LPFC_EVENT_BUSRESET	0x0020
#define LPFC_EVENT_VARQUEDEPTH	0x0040

/* special case scsi varqueuedepth event */
struct lpfc_scsi_varqueuedepth_event {
	struct lpfc_scsi_event_header scsi_event;
	uint32_t oldval;
	uint32_t newval;
};

/* special case scsi check condition event */
struct lpfc_scsi_check_condition_event {
	struct lpfc_scsi_event_header scsi_event;
	uint8_t opcode;
	uint8_t sense_key;
	uint8_t asc;
	uint8_t ascq;
};

/* event codes for FC_REG_BOARD_EVENT */
#define LPFC_EVENT_PORTINTERR		0x01

/* board event header */
struct lpfc_board_event_header {
	uint32_t event_type;
	uint32_t subcategory;
};


/* event codes for FC_REG_ADAPTER_EVENT */
#define LPFC_EVENT_ARRIVAL	0x01

/* adapter event header */
struct lpfc_adapter_event_header {
	uint32_t event_type;
	uint32_t subcategory;
};


/* event codes for temp_event */
#define LPFC_CRIT_TEMP		0x1
#define LPFC_THRESHOLD_TEMP	0x2
#define LPFC_NORMAL_TEMP	0x3

struct temp_event {
	uint32_t event_type;
	uint32_t event_code;
	uint32_t data;
};

