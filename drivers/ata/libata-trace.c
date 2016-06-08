/*
 * libata-trace.c - trace functions for libata
 *
 * Copyright 2015 Hannes Reinecke
 * Copyright 2015 SUSE Linux GmbH
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/trace_seq.h>
#include <trace/events/libata.h>

const char *
libata_trace_parse_status(struct trace_seq *p, unsigned char status)
{
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_printf(p, "{ ");
	if (status & ATA_BUSY)
		trace_seq_printf(p, "BUSY ");
	if (status & ATA_DRDY)
		trace_seq_printf(p, "DRDY ");
	if (status & ATA_DF)
		trace_seq_printf(p, "DF ");
	if (status & ATA_DSC)
		trace_seq_printf(p, "DSC ");
	if (status & ATA_DRQ)
		trace_seq_printf(p, "DRQ ");
	if (status & ATA_CORR)
		trace_seq_printf(p, "CORR ");
	if (status & ATA_SENSE)
		trace_seq_printf(p, "SENSE ");
	if (status & ATA_ERR)
		trace_seq_printf(p, "ERR ");
	trace_seq_putc(p, '}');
	trace_seq_putc(p, 0);

	return ret;
}

const char *
libata_trace_parse_eh_action(struct trace_seq *p, unsigned int eh_action)
{
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_printf(p, "%x", eh_action);
	if (eh_action) {
		trace_seq_printf(p, "{ ");
		if (eh_action & ATA_EH_REVALIDATE)
			trace_seq_printf(p, "REVALIDATE ");
		if (eh_action & (ATA_EH_SOFTRESET | ATA_EH_HARDRESET))
			trace_seq_printf(p, "RESET ");
		else if (eh_action & ATA_EH_SOFTRESET)
			trace_seq_printf(p, "SOFTRESET ");
		else if (eh_action & ATA_EH_HARDRESET)
			trace_seq_printf(p, "HARDRESET ");
		if (eh_action & ATA_EH_ENABLE_LINK)
			trace_seq_printf(p, "ENABLE_LINK ");
		if (eh_action & ATA_EH_PARK)
			trace_seq_printf(p, "PARK ");
		trace_seq_putc(p, '}');
	}
	trace_seq_putc(p, 0);

	return ret;
}

const char *
libata_trace_parse_eh_err_mask(struct trace_seq *p, unsigned int eh_err_mask)
{
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_printf(p, "%x", eh_err_mask);
	if (eh_err_mask) {
		trace_seq_printf(p, "{ ");
		if (eh_err_mask & AC_ERR_DEV)
			trace_seq_printf(p, "DEV ");
		if (eh_err_mask & AC_ERR_HSM)
			trace_seq_printf(p, "HSM ");
		if (eh_err_mask & AC_ERR_TIMEOUT)
			trace_seq_printf(p, "TIMEOUT ");
		if (eh_err_mask & AC_ERR_MEDIA)
			trace_seq_printf(p, "MEDIA ");
		if (eh_err_mask & AC_ERR_ATA_BUS)
			trace_seq_printf(p, "ATA_BUS ");
		if (eh_err_mask & AC_ERR_HOST_BUS)
			trace_seq_printf(p, "HOST_BUS ");
		if (eh_err_mask & AC_ERR_SYSTEM)
			trace_seq_printf(p, "SYSTEM ");
		if (eh_err_mask & AC_ERR_INVALID)
			trace_seq_printf(p, "INVALID ");
		if (eh_err_mask & AC_ERR_OTHER)
			trace_seq_printf(p, "OTHER ");
		if (eh_err_mask & AC_ERR_NODEV_HINT)
			trace_seq_printf(p, "NODEV_HINT ");
		if (eh_err_mask & AC_ERR_NCQ)
			trace_seq_printf(p, "NCQ ");
		trace_seq_putc(p, '}');
	}
	trace_seq_putc(p, 0);

	return ret;
}

const char *
libata_trace_parse_qc_flags(struct trace_seq *p, unsigned int qc_flags)
{
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_printf(p, "%x", qc_flags);
	if (qc_flags) {
		trace_seq_printf(p, "{ ");
		if (qc_flags & ATA_QCFLAG_ACTIVE)
			trace_seq_printf(p, "ACTIVE ");
		if (qc_flags & ATA_QCFLAG_DMAMAP)
			trace_seq_printf(p, "DMAMAP ");
		if (qc_flags & ATA_QCFLAG_IO)
			trace_seq_printf(p, "IO ");
		if (qc_flags & ATA_QCFLAG_RESULT_TF)
			trace_seq_printf(p, "RESULT_TF ");
		if (qc_flags & ATA_QCFLAG_CLEAR_EXCL)
			trace_seq_printf(p, "CLEAR_EXCL ");
		if (qc_flags & ATA_QCFLAG_QUIET)
			trace_seq_printf(p, "QUIET ");
		if (qc_flags & ATA_QCFLAG_RETRY)
			trace_seq_printf(p, "RETRY ");
		if (qc_flags & ATA_QCFLAG_FAILED)
			trace_seq_printf(p, "FAILED ");
		if (qc_flags & ATA_QCFLAG_SENSE_VALID)
			trace_seq_printf(p, "SENSE_VALID ");
		if (qc_flags & ATA_QCFLAG_EH_SCHEDULED)
			trace_seq_printf(p, "EH_SCHEDULED ");
		trace_seq_putc(p, '}');
	}
	trace_seq_putc(p, 0);

	return ret;
}

const char *
libata_trace_parse_subcmd(struct trace_seq *p, unsigned char cmd,
			  unsigned char feature, unsigned char hob_nsect)
{
	const char *ret = trace_seq_buffer_ptr(p);

	switch (cmd) {
	case ATA_CMD_FPDMA_RECV:
		switch (hob_nsect & 0x5f) {
		case ATA_SUBCMD_FPDMA_RECV_RD_LOG_DMA_EXT:
			trace_seq_printf(p, " READ_LOG_DMA_EXT");
			break;
		case ATA_SUBCMD_FPDMA_RECV_ZAC_MGMT_IN:
			trace_seq_printf(p, " ZAC_MGMT_IN");
			break;
		}
		break;
	case ATA_CMD_FPDMA_SEND:
		switch (hob_nsect & 0x5f) {
		case ATA_SUBCMD_FPDMA_SEND_WR_LOG_DMA_EXT:
			trace_seq_printf(p, " WRITE_LOG_DMA_EXT");
			break;
		case ATA_SUBCMD_FPDMA_SEND_DSM:
			trace_seq_printf(p, " DATASET_MANAGEMENT");
			break;
		}
		break;
	case ATA_CMD_NCQ_NON_DATA:
		switch (feature) {
		case ATA_SUBCMD_NCQ_NON_DATA_ABORT_QUEUE:
			trace_seq_printf(p, " ABORT_QUEUE");
			break;
		case ATA_SUBCMD_NCQ_NON_DATA_SET_FEATURES:
			trace_seq_printf(p, " SET_FEATURES");
			break;
		case ATA_SUBCMD_NCQ_NON_DATA_ZERO_EXT:
			trace_seq_printf(p, " ZERO_EXT");
			break;
		case ATA_SUBCMD_NCQ_NON_DATA_ZAC_MGMT_OUT:
			trace_seq_printf(p, " ZAC_MGMT_OUT");
			break;
		}
		break;
	case ATA_CMD_ZAC_MGMT_IN:
		switch (feature) {
		case ATA_SUBCMD_ZAC_MGMT_IN_REPORT_ZONES:
			trace_seq_printf(p, " REPORT_ZONES");
			break;
		}
		break;
	case ATA_CMD_ZAC_MGMT_OUT:
		switch (feature) {
		case ATA_SUBCMD_ZAC_MGMT_OUT_CLOSE_ZONE:
			trace_seq_printf(p, " CLOSE_ZONE");
			break;
		case ATA_SUBCMD_ZAC_MGMT_OUT_FINISH_ZONE:
			trace_seq_printf(p, " FINISH_ZONE");
			break;
		case ATA_SUBCMD_ZAC_MGMT_OUT_OPEN_ZONE:
			trace_seq_printf(p, " OPEN_ZONE");
			break;
		case ATA_SUBCMD_ZAC_MGMT_OUT_RESET_WRITE_POINTER:
			trace_seq_printf(p, " RESET_WRITE_POINTER");
			break;
		}
		break;
	}
	trace_seq_putc(p, 0);

	return ret;
}
