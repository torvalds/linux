/*
 * PMC-Sierra SPCv/ve 8088/8089 SAS/SATA based host adapters driver
 *
 * Copyright (c) 2008-2009 PMC-Sierra, Inc.,
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions, and the following disclaimer,
 * without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 * substantially similar to the "NO WARRANTY" disclaimer below
 * ("Disclaimer") and any redistribution must be conditioned upon
 * including a substantially similar Disclaimer requirement for further
 * binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 * of any contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 */
 #include <linux/slab.h>
 #include "pm8001_sas.h"
 #include "pm80xx_hwi.h"
 #include "pm8001_chips.h"
 #include "pm8001_ctl.h"
#include "pm80xx_tracepoints.h"

#define SMP_DIRECT 1
#define SMP_INDIRECT 2


int pm80xx_bar4_shift(struct pm8001_hba_info *pm8001_ha, u32 shift_value)
{
	u32 reg_val;
	unsigned long start;
	pm8001_cw32(pm8001_ha, 0, MEMBASE_II_SHIFT_REGISTER, shift_value);
	/* confirm the setting is written */
	start = jiffies + HZ; /* 1 sec */
	do {
		reg_val = pm8001_cr32(pm8001_ha, 0, MEMBASE_II_SHIFT_REGISTER);
	} while ((reg_val != shift_value) && time_before(jiffies, start));
	if (reg_val != shift_value) {
		pm8001_dbg(pm8001_ha, FAIL, "TIMEOUT:MEMBASE_II_SHIFT_REGISTER = 0x%x\n",
			   reg_val);
		return -1;
	}
	return 0;
}

static void pm80xx_pci_mem_copy(struct pm8001_hba_info  *pm8001_ha, u32 soffset,
				const void *destination,
				u32 dw_count, u32 bus_base_number)
{
	u32 index, value, offset;
	u32 *destination1;
	destination1 = (u32 *)destination;

	for (index = 0; index < dw_count; index += 4, destination1++) {
		offset = (soffset + index);
		if (offset < (64 * 1024)) {
			value = pm8001_cr32(pm8001_ha, bus_base_number, offset);
			*destination1 =  cpu_to_le32(value);
		}
	}
	return;
}

ssize_t pm80xx_get_fatal_dump(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;
	void __iomem *fatal_table_address = pm8001_ha->fatal_tbl_addr;
	u32 accum_len, reg_val, index, *temp;
	u32 status = 1;
	unsigned long start;
	u8 *direct_data;
	char *fatal_error_data = buf;
	u32 length_to_read;
	u32 offset;

	pm8001_ha->forensic_info.data_buf.direct_data = buf;
	if (pm8001_ha->chip_id == chip_8001) {
		pm8001_ha->forensic_info.data_buf.direct_data +=
			sprintf(pm8001_ha->forensic_info.data_buf.direct_data,
			"Not supported for SPC controller");
		return (char *)pm8001_ha->forensic_info.data_buf.direct_data -
			(char *)buf;
	}
	/* initialize variables for very first call from host application */
	if (pm8001_ha->forensic_info.data_buf.direct_offset == 0) {
		pm8001_dbg(pm8001_ha, IO,
			   "forensic_info TYPE_NON_FATAL..............\n");
		direct_data = (u8 *)fatal_error_data;
		pm8001_ha->forensic_info.data_type = TYPE_NON_FATAL;
		pm8001_ha->forensic_info.data_buf.direct_len = SYSFS_OFFSET;
		pm8001_ha->forensic_info.data_buf.direct_offset = 0;
		pm8001_ha->forensic_info.data_buf.read_len = 0;
		pm8001_ha->forensic_preserved_accumulated_transfer = 0;

		/* Write signature to fatal dump table */
		pm8001_mw32(fatal_table_address,
				MPI_FATAL_EDUMP_TABLE_SIGNATURE, 0x1234abcd);

		pm8001_ha->forensic_info.data_buf.direct_data = direct_data;
		pm8001_dbg(pm8001_ha, IO, "ossaHwCB: status1 %d\n", status);
		pm8001_dbg(pm8001_ha, IO, "ossaHwCB: read_len 0x%x\n",
			   pm8001_ha->forensic_info.data_buf.read_len);
		pm8001_dbg(pm8001_ha, IO, "ossaHwCB: direct_len 0x%x\n",
			   pm8001_ha->forensic_info.data_buf.direct_len);
		pm8001_dbg(pm8001_ha, IO, "ossaHwCB: direct_offset 0x%x\n",
			   pm8001_ha->forensic_info.data_buf.direct_offset);
	}
	if (pm8001_ha->forensic_info.data_buf.direct_offset == 0) {
		/* start to get data */
		/* Program the MEMBASE II Shifting Register with 0x00.*/
		pm8001_cw32(pm8001_ha, 0, MEMBASE_II_SHIFT_REGISTER,
				pm8001_ha->fatal_forensic_shift_offset);
		pm8001_ha->forensic_last_offset = 0;
		pm8001_ha->forensic_fatal_step = 0;
		pm8001_ha->fatal_bar_loc = 0;
	}

	/* Read until accum_len is retrieved */
	accum_len = pm8001_mr32(fatal_table_address,
				MPI_FATAL_EDUMP_TABLE_ACCUM_LEN);
	/* Determine length of data between previously stored transfer length
	 * and current accumulated transfer length
	 */
	length_to_read =
		accum_len - pm8001_ha->forensic_preserved_accumulated_transfer;
	pm8001_dbg(pm8001_ha, IO, "get_fatal_spcv: accum_len 0x%x\n",
		   accum_len);
	pm8001_dbg(pm8001_ha, IO, "get_fatal_spcv: length_to_read 0x%x\n",
		   length_to_read);
	pm8001_dbg(pm8001_ha, IO, "get_fatal_spcv: last_offset 0x%x\n",
		   pm8001_ha->forensic_last_offset);
	pm8001_dbg(pm8001_ha, IO, "get_fatal_spcv: read_len 0x%x\n",
		   pm8001_ha->forensic_info.data_buf.read_len);
	pm8001_dbg(pm8001_ha, IO, "get_fatal_spcv:: direct_len 0x%x\n",
		   pm8001_ha->forensic_info.data_buf.direct_len);
	pm8001_dbg(pm8001_ha, IO, "get_fatal_spcv:: direct_offset 0x%x\n",
		   pm8001_ha->forensic_info.data_buf.direct_offset);

	/* If accumulated length failed to read correctly fail the attempt.*/
	if (accum_len == 0xFFFFFFFF) {
		pm8001_dbg(pm8001_ha, IO,
			   "Possible PCI issue 0x%x not expected\n",
			   accum_len);
		return status;
	}
	/* If accumulated length is zero fail the attempt */
	if (accum_len == 0) {
		pm8001_ha->forensic_info.data_buf.direct_data +=
			sprintf(pm8001_ha->forensic_info.data_buf.direct_data,
			"%08x ", 0xFFFFFFFF);
		return (char *)pm8001_ha->forensic_info.data_buf.direct_data -
			(char *)buf;
	}
	/* Accumulated length is good so start capturing the first data */
	temp = (u32 *)pm8001_ha->memoryMap.region[FORENSIC_MEM].virt_ptr;
	if (pm8001_ha->forensic_fatal_step == 0) {
moreData:
		/* If data to read is less than SYSFS_OFFSET then reduce the
		 * length of dataLen
		 */
		if (pm8001_ha->forensic_last_offset + SYSFS_OFFSET
				> length_to_read) {
			pm8001_ha->forensic_info.data_buf.direct_len =
				length_to_read -
				pm8001_ha->forensic_last_offset;
		} else {
			pm8001_ha->forensic_info.data_buf.direct_len =
				SYSFS_OFFSET;
		}
		if (pm8001_ha->forensic_info.data_buf.direct_data) {
			/* Data is in bar, copy to host memory */
			pm80xx_pci_mem_copy(pm8001_ha,
			pm8001_ha->fatal_bar_loc,
			pm8001_ha->memoryMap.region[FORENSIC_MEM].virt_ptr,
			pm8001_ha->forensic_info.data_buf.direct_len, 1);
		}
		pm8001_ha->fatal_bar_loc +=
			pm8001_ha->forensic_info.data_buf.direct_len;
		pm8001_ha->forensic_info.data_buf.direct_offset +=
			pm8001_ha->forensic_info.data_buf.direct_len;
		pm8001_ha->forensic_last_offset	+=
			pm8001_ha->forensic_info.data_buf.direct_len;
		pm8001_ha->forensic_info.data_buf.read_len =
			pm8001_ha->forensic_info.data_buf.direct_len;

		if (pm8001_ha->forensic_last_offset  >= length_to_read) {
			pm8001_ha->forensic_info.data_buf.direct_data +=
			sprintf(pm8001_ha->forensic_info.data_buf.direct_data,
				"%08x ", 3);
			for (index = 0; index <
				(pm8001_ha->forensic_info.data_buf.direct_len
				 / 4); index++) {
				pm8001_ha->forensic_info.data_buf.direct_data +=
				sprintf(
				pm8001_ha->forensic_info.data_buf.direct_data,
				"%08x ", *(temp + index));
			}

			pm8001_ha->fatal_bar_loc = 0;
			pm8001_ha->forensic_fatal_step = 1;
			pm8001_ha->fatal_forensic_shift_offset = 0;
			pm8001_ha->forensic_last_offset	= 0;
			status = 0;
			offset = (int)
			((char *)pm8001_ha->forensic_info.data_buf.direct_data
			- (char *)buf);
			pm8001_dbg(pm8001_ha, IO,
				   "get_fatal_spcv:return1 0x%x\n", offset);
			return (char *)pm8001_ha->
				forensic_info.data_buf.direct_data -
				(char *)buf;
		}
		if (pm8001_ha->fatal_bar_loc < (64 * 1024)) {
			pm8001_ha->forensic_info.data_buf.direct_data +=
				sprintf(pm8001_ha->
					forensic_info.data_buf.direct_data,
					"%08x ", 2);
			for (index = 0; index <
				(pm8001_ha->forensic_info.data_buf.direct_len
				 / 4); index++) {
				pm8001_ha->forensic_info.data_buf.direct_data
					+= sprintf(pm8001_ha->
					forensic_info.data_buf.direct_data,
					"%08x ", *(temp + index));
			}
			status = 0;
			offset = (int)
			((char *)pm8001_ha->forensic_info.data_buf.direct_data
			- (char *)buf);
			pm8001_dbg(pm8001_ha, IO,
				   "get_fatal_spcv:return2 0x%x\n", offset);
			return (char *)pm8001_ha->
				forensic_info.data_buf.direct_data -
				(char *)buf;
		}

		/* Increment the MEMBASE II Shifting Register value by 0x100.*/
		pm8001_ha->forensic_info.data_buf.direct_data +=
			sprintf(pm8001_ha->forensic_info.data_buf.direct_data,
				"%08x ", 2);
		for (index = 0; index <
			(pm8001_ha->forensic_info.data_buf.direct_len
			 / 4) ; index++) {
			pm8001_ha->forensic_info.data_buf.direct_data +=
				sprintf(pm8001_ha->
				forensic_info.data_buf.direct_data,
				"%08x ", *(temp + index));
		}
		pm8001_ha->fatal_forensic_shift_offset += 0x100;
		pm8001_cw32(pm8001_ha, 0, MEMBASE_II_SHIFT_REGISTER,
			pm8001_ha->fatal_forensic_shift_offset);
		pm8001_ha->fatal_bar_loc = 0;
		status = 0;
		offset = (int)
			((char *)pm8001_ha->forensic_info.data_buf.direct_data
			- (char *)buf);
		pm8001_dbg(pm8001_ha, IO, "get_fatal_spcv: return3 0x%x\n",
			   offset);
		return (char *)pm8001_ha->forensic_info.data_buf.direct_data -
			(char *)buf;
	}
	if (pm8001_ha->forensic_fatal_step == 1) {
		/* store previous accumulated length before triggering next
		 * accumulated length update
		 */
		pm8001_ha->forensic_preserved_accumulated_transfer =
			pm8001_mr32(fatal_table_address,
			MPI_FATAL_EDUMP_TABLE_ACCUM_LEN);

		/* continue capturing the fatal log until Dump status is 0x3 */
		if (pm8001_mr32(fatal_table_address,
			MPI_FATAL_EDUMP_TABLE_STATUS) <
			MPI_FATAL_EDUMP_TABLE_STAT_NF_SUCCESS_DONE) {

			/* reset fddstat bit by writing to zero*/
			pm8001_mw32(fatal_table_address,
					MPI_FATAL_EDUMP_TABLE_STATUS, 0x0);

			/* set dump control value to '1' so that new data will
			 * be transferred to shared memory
			 */
			pm8001_mw32(fatal_table_address,
				MPI_FATAL_EDUMP_TABLE_HANDSHAKE,
				MPI_FATAL_EDUMP_HANDSHAKE_RDY);

			/*Poll FDDHSHK  until clear */
			start = jiffies + (2 * HZ); /* 2 sec */

			do {
				reg_val = pm8001_mr32(fatal_table_address,
					MPI_FATAL_EDUMP_TABLE_HANDSHAKE);
			} while ((reg_val) && time_before(jiffies, start));

			if (reg_val != 0) {
				pm8001_dbg(pm8001_ha, FAIL,
					   "TIMEOUT:MPI_FATAL_EDUMP_TABLE_HDSHAKE 0x%x\n",
					   reg_val);
			       /* Fail the dump if a timeout occurs */
				pm8001_ha->forensic_info.data_buf.direct_data +=
				sprintf(
				pm8001_ha->forensic_info.data_buf.direct_data,
				"%08x ", 0xFFFFFFFF);
				return((char *)
				pm8001_ha->forensic_info.data_buf.direct_data
				- (char *)buf);
			}
			/* Poll status register until set to 2 or
			 * 3 for up to 2 seconds
			 */
			start = jiffies + (2 * HZ); /* 2 sec */

			do {
				reg_val = pm8001_mr32(fatal_table_address,
					MPI_FATAL_EDUMP_TABLE_STATUS);
			} while (((reg_val != 2) && (reg_val != 3)) &&
					time_before(jiffies, start));

			if (reg_val < 2) {
				pm8001_dbg(pm8001_ha, FAIL,
					   "TIMEOUT:MPI_FATAL_EDUMP_TABLE_STATUS = 0x%x\n",
					   reg_val);
				/* Fail the dump if a timeout occurs */
				pm8001_ha->forensic_info.data_buf.direct_data +=
				sprintf(
				pm8001_ha->forensic_info.data_buf.direct_data,
				"%08x ", 0xFFFFFFFF);
				return((char *)pm8001_ha->forensic_info.data_buf.direct_data -
						(char *)buf);
			}
	/* reset fatal_forensic_shift_offset back to zero and reset MEMBASE 2 register to zero */
			pm8001_ha->fatal_forensic_shift_offset = 0; /* location in 64k region */
			pm8001_cw32(pm8001_ha, 0,
					MEMBASE_II_SHIFT_REGISTER,
					pm8001_ha->fatal_forensic_shift_offset);
		}
		/* Read the next block of the debug data.*/
		length_to_read = pm8001_mr32(fatal_table_address,
		MPI_FATAL_EDUMP_TABLE_ACCUM_LEN) -
		pm8001_ha->forensic_preserved_accumulated_transfer;
		if (length_to_read != 0x0) {
			pm8001_ha->forensic_fatal_step = 0;
			goto moreData;
		} else {
			pm8001_ha->forensic_info.data_buf.direct_data +=
			sprintf(pm8001_ha->forensic_info.data_buf.direct_data,
				"%08x ", 4);
			pm8001_ha->forensic_info.data_buf.read_len = 0xFFFFFFFF;
			pm8001_ha->forensic_info.data_buf.direct_len =  0;
			pm8001_ha->forensic_info.data_buf.direct_offset = 0;
			pm8001_ha->forensic_info.data_buf.read_len = 0;
		}
	}
	offset = (int)((char *)pm8001_ha->forensic_info.data_buf.direct_data
			- (char *)buf);
	pm8001_dbg(pm8001_ha, IO, "get_fatal_spcv: return4 0x%x\n", offset);
	return ((char *)pm8001_ha->forensic_info.data_buf.direct_data -
		(char *)buf);
}

/* pm80xx_get_non_fatal_dump - dump the nonfatal data from the dma
 * location by the firmware.
 */
ssize_t pm80xx_get_non_fatal_dump(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;
	void __iomem *nonfatal_table_address = pm8001_ha->fatal_tbl_addr;
	u32 accum_len = 0;
	u32 total_len = 0;
	u32 reg_val = 0;
	u32 *temp = NULL;
	u32 index = 0;
	u32 output_length;
	unsigned long start = 0;
	char *buf_copy = buf;

	temp = (u32 *)pm8001_ha->memoryMap.region[FORENSIC_MEM].virt_ptr;
	if (++pm8001_ha->non_fatal_count == 1) {
		if (pm8001_ha->chip_id == chip_8001) {
			snprintf(pm8001_ha->forensic_info.data_buf.direct_data,
				PAGE_SIZE, "Not supported for SPC controller");
			return 0;
		}
		pm8001_dbg(pm8001_ha, IO, "forensic_info TYPE_NON_FATAL...\n");
		/*
		 * Step 1: Write the host buffer parameters in the MPI Fatal and
		 * Non-Fatal Error Dump Capture Table.This is the buffer
		 * where debug data will be DMAed to.
		 */
		pm8001_mw32(nonfatal_table_address,
		MPI_FATAL_EDUMP_TABLE_LO_OFFSET,
		pm8001_ha->memoryMap.region[FORENSIC_MEM].phys_addr_lo);

		pm8001_mw32(nonfatal_table_address,
		MPI_FATAL_EDUMP_TABLE_HI_OFFSET,
		pm8001_ha->memoryMap.region[FORENSIC_MEM].phys_addr_hi);

		pm8001_mw32(nonfatal_table_address,
		MPI_FATAL_EDUMP_TABLE_LENGTH, SYSFS_OFFSET);

		/* Optionally, set the DUMPCTRL bit to 1 if the host
		 * keeps sending active I/Os while capturing the non-fatal
		 * debug data. Otherwise, leave this bit set to zero
		 */
		pm8001_mw32(nonfatal_table_address,
		MPI_FATAL_EDUMP_TABLE_HANDSHAKE, MPI_FATAL_EDUMP_HANDSHAKE_RDY);

		/*
		 * Step 2: Clear Accumulative Length of Debug Data Transferred
		 * [ACCDDLEN] field in the MPI Fatal and Non-Fatal Error Dump
		 * Capture Table to zero.
		 */
		pm8001_mw32(nonfatal_table_address,
				MPI_FATAL_EDUMP_TABLE_ACCUM_LEN, 0);

		/* initiallize previous accumulated length to 0 */
		pm8001_ha->forensic_preserved_accumulated_transfer = 0;
		pm8001_ha->non_fatal_read_length = 0;
	}

	total_len = pm8001_mr32(nonfatal_table_address,
			MPI_FATAL_EDUMP_TABLE_TOTAL_LEN);
	/*
	 * Step 3:Clear Fatal/Non-Fatal Debug Data Transfer Status [FDDTSTAT]
	 * field and then request that the SPCv controller transfer the debug
	 * data by setting bit 7 of the Inbound Doorbell Set Register.
	 */
	pm8001_mw32(nonfatal_table_address, MPI_FATAL_EDUMP_TABLE_STATUS, 0);
	pm8001_cw32(pm8001_ha, 0, MSGU_IBDB_SET,
			SPCv_MSGU_CFG_TABLE_NONFATAL_DUMP);

	/*
	 * Step 4.1: Read back the Inbound Doorbell Set Register (by polling for
	 * 2 seconds) until register bit 7 is cleared.
	 * This step only indicates the request is accepted by the controller.
	 */
	start = jiffies + (2 * HZ); /* 2 sec */
	do {
		reg_val = pm8001_cr32(pm8001_ha, 0, MSGU_IBDB_SET) &
			SPCv_MSGU_CFG_TABLE_NONFATAL_DUMP;
	} while ((reg_val != 0) && time_before(jiffies, start));

	/* Step 4.2: To check the completion of the transfer, poll the Fatal/Non
	 * Fatal Debug Data Transfer Status [FDDTSTAT] field for 2 seconds in
	 * the MPI Fatal and Non-Fatal Error Dump Capture Table.
	 */
	start = jiffies + (2 * HZ); /* 2 sec */
	do {
		reg_val = pm8001_mr32(nonfatal_table_address,
				MPI_FATAL_EDUMP_TABLE_STATUS);
	} while ((!reg_val) && time_before(jiffies, start));

	if ((reg_val == 0x00) ||
		(reg_val == MPI_FATAL_EDUMP_TABLE_STAT_DMA_FAILED) ||
		(reg_val > MPI_FATAL_EDUMP_TABLE_STAT_NF_SUCCESS_DONE)) {
		pm8001_ha->non_fatal_read_length = 0;
		buf_copy += snprintf(buf_copy, PAGE_SIZE, "%08x ", 0xFFFFFFFF);
		pm8001_ha->non_fatal_count = 0;
		return (buf_copy - buf);
	} else if (reg_val ==
			MPI_FATAL_EDUMP_TABLE_STAT_NF_SUCCESS_MORE_DATA) {
		buf_copy += snprintf(buf_copy, PAGE_SIZE, "%08x ", 2);
	} else if ((reg_val == MPI_FATAL_EDUMP_TABLE_STAT_NF_SUCCESS_DONE) ||
		(pm8001_ha->non_fatal_read_length >= total_len)) {
		pm8001_ha->non_fatal_read_length = 0;
		buf_copy += snprintf(buf_copy, PAGE_SIZE, "%08x ", 4);
		pm8001_ha->non_fatal_count = 0;
	}
	accum_len = pm8001_mr32(nonfatal_table_address,
			MPI_FATAL_EDUMP_TABLE_ACCUM_LEN);
	output_length = accum_len -
		pm8001_ha->forensic_preserved_accumulated_transfer;

	for (index = 0; index < output_length/4; index++)
		buf_copy += snprintf(buf_copy, PAGE_SIZE,
				"%08x ", *(temp+index));

	pm8001_ha->non_fatal_read_length += output_length;

	/* store current accumulated length to use in next iteration as
	 * the previous accumulated length
	 */
	pm8001_ha->forensic_preserved_accumulated_transfer = accum_len;
	return (buf_copy - buf);
}

/**
 * read_main_config_table - read the configure table and save it.
 * @pm8001_ha: our hba card information
 */
static void read_main_config_table(struct pm8001_hba_info *pm8001_ha)
{
	void __iomem *address = pm8001_ha->main_cfg_tbl_addr;

	pm8001_ha->main_cfg_tbl.pm80xx_tbl.signature	=
		pm8001_mr32(address, MAIN_SIGNATURE_OFFSET);
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.interface_rev =
		pm8001_mr32(address, MAIN_INTERFACE_REVISION);
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.firmware_rev	=
		pm8001_mr32(address, MAIN_FW_REVISION);
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.max_out_io	=
		pm8001_mr32(address, MAIN_MAX_OUTSTANDING_IO_OFFSET);
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.max_sgl	=
		pm8001_mr32(address, MAIN_MAX_SGL_OFFSET);
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.ctrl_cap_flag =
		pm8001_mr32(address, MAIN_CNTRL_CAP_OFFSET);
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.gst_offset	=
		pm8001_mr32(address, MAIN_GST_OFFSET);
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.inbound_queue_offset =
		pm8001_mr32(address, MAIN_IBQ_OFFSET);
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.outbound_queue_offset =
		pm8001_mr32(address, MAIN_OBQ_OFFSET);

	/* read Error Dump Offset and Length */
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.fatal_err_dump_offset0 =
		pm8001_mr32(address, MAIN_FATAL_ERROR_RDUMP0_OFFSET);
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.fatal_err_dump_length0 =
		pm8001_mr32(address, MAIN_FATAL_ERROR_RDUMP0_LENGTH);
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.fatal_err_dump_offset1 =
		pm8001_mr32(address, MAIN_FATAL_ERROR_RDUMP1_OFFSET);
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.fatal_err_dump_length1 =
		pm8001_mr32(address, MAIN_FATAL_ERROR_RDUMP1_LENGTH);

	/* read GPIO LED settings from the configuration table */
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.gpio_led_mapping =
		pm8001_mr32(address, MAIN_GPIO_LED_FLAGS_OFFSET);

	/* read analog Setting offset from the configuration table */
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.analog_setup_table_offset =
		pm8001_mr32(address, MAIN_ANALOG_SETUP_OFFSET);

	pm8001_ha->main_cfg_tbl.pm80xx_tbl.int_vec_table_offset =
		pm8001_mr32(address, MAIN_INT_VECTOR_TABLE_OFFSET);
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.phy_attr_table_offset =
		pm8001_mr32(address, MAIN_SAS_PHY_ATTR_TABLE_OFFSET);
	/* read port recover and reset timeout */
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.port_recovery_timer =
		pm8001_mr32(address, MAIN_PORT_RECOVERY_TIMER);
	/* read ILA and inactive firmware version */
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.ila_version =
		pm8001_mr32(address, MAIN_MPI_ILA_RELEASE_TYPE);
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.inc_fw_version =
		pm8001_mr32(address, MAIN_MPI_INACTIVE_FW_VERSION);

	pm8001_dbg(pm8001_ha, DEV,
		   "Main cfg table: sign:%x interface rev:%x fw_rev:%x\n",
		   pm8001_ha->main_cfg_tbl.pm80xx_tbl.signature,
		   pm8001_ha->main_cfg_tbl.pm80xx_tbl.interface_rev,
		   pm8001_ha->main_cfg_tbl.pm80xx_tbl.firmware_rev);

	pm8001_dbg(pm8001_ha, DEV,
		   "table offset: gst:%x iq:%x oq:%x int vec:%x phy attr:%x\n",
		   pm8001_ha->main_cfg_tbl.pm80xx_tbl.gst_offset,
		   pm8001_ha->main_cfg_tbl.pm80xx_tbl.inbound_queue_offset,
		   pm8001_ha->main_cfg_tbl.pm80xx_tbl.outbound_queue_offset,
		   pm8001_ha->main_cfg_tbl.pm80xx_tbl.int_vec_table_offset,
		   pm8001_ha->main_cfg_tbl.pm80xx_tbl.phy_attr_table_offset);

	pm8001_dbg(pm8001_ha, DEV,
		   "Main cfg table; ila rev:%x Inactive fw rev:%x\n",
		   pm8001_ha->main_cfg_tbl.pm80xx_tbl.ila_version,
		   pm8001_ha->main_cfg_tbl.pm80xx_tbl.inc_fw_version);
}

/**
 * read_general_status_table - read the general status table and save it.
 * @pm8001_ha: our hba card information
 */
static void read_general_status_table(struct pm8001_hba_info *pm8001_ha)
{
	void __iomem *address = pm8001_ha->general_stat_tbl_addr;
	pm8001_ha->gs_tbl.pm80xx_tbl.gst_len_mpistate	=
			pm8001_mr32(address, GST_GSTLEN_MPIS_OFFSET);
	pm8001_ha->gs_tbl.pm80xx_tbl.iq_freeze_state0	=
			pm8001_mr32(address, GST_IQ_FREEZE_STATE0_OFFSET);
	pm8001_ha->gs_tbl.pm80xx_tbl.iq_freeze_state1	=
			pm8001_mr32(address, GST_IQ_FREEZE_STATE1_OFFSET);
	pm8001_ha->gs_tbl.pm80xx_tbl.msgu_tcnt		=
			pm8001_mr32(address, GST_MSGUTCNT_OFFSET);
	pm8001_ha->gs_tbl.pm80xx_tbl.iop_tcnt		=
			pm8001_mr32(address, GST_IOPTCNT_OFFSET);
	pm8001_ha->gs_tbl.pm80xx_tbl.gpio_input_val	=
			pm8001_mr32(address, GST_GPIO_INPUT_VAL);
	pm8001_ha->gs_tbl.pm80xx_tbl.recover_err_info[0] =
			pm8001_mr32(address, GST_RERRINFO_OFFSET0);
	pm8001_ha->gs_tbl.pm80xx_tbl.recover_err_info[1] =
			pm8001_mr32(address, GST_RERRINFO_OFFSET1);
	pm8001_ha->gs_tbl.pm80xx_tbl.recover_err_info[2] =
			pm8001_mr32(address, GST_RERRINFO_OFFSET2);
	pm8001_ha->gs_tbl.pm80xx_tbl.recover_err_info[3] =
			pm8001_mr32(address, GST_RERRINFO_OFFSET3);
	pm8001_ha->gs_tbl.pm80xx_tbl.recover_err_info[4] =
			pm8001_mr32(address, GST_RERRINFO_OFFSET4);
	pm8001_ha->gs_tbl.pm80xx_tbl.recover_err_info[5] =
			pm8001_mr32(address, GST_RERRINFO_OFFSET5);
	pm8001_ha->gs_tbl.pm80xx_tbl.recover_err_info[6] =
			pm8001_mr32(address, GST_RERRINFO_OFFSET6);
	pm8001_ha->gs_tbl.pm80xx_tbl.recover_err_info[7] =
			 pm8001_mr32(address, GST_RERRINFO_OFFSET7);
}
/**
 * read_phy_attr_table - read the phy attribute table and save it.
 * @pm8001_ha: our hba card information
 */
static void read_phy_attr_table(struct pm8001_hba_info *pm8001_ha)
{
	void __iomem *address = pm8001_ha->pspa_q_tbl_addr;
	pm8001_ha->phy_attr_table.phystart1_16[0] =
			pm8001_mr32(address, PSPA_PHYSTATE0_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[1] =
			pm8001_mr32(address, PSPA_PHYSTATE1_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[2] =
			pm8001_mr32(address, PSPA_PHYSTATE2_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[3] =
			pm8001_mr32(address, PSPA_PHYSTATE3_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[4] =
			pm8001_mr32(address, PSPA_PHYSTATE4_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[5] =
			pm8001_mr32(address, PSPA_PHYSTATE5_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[6] =
			pm8001_mr32(address, PSPA_PHYSTATE6_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[7] =
			pm8001_mr32(address, PSPA_PHYSTATE7_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[8] =
			pm8001_mr32(address, PSPA_PHYSTATE8_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[9] =
			pm8001_mr32(address, PSPA_PHYSTATE9_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[10] =
			pm8001_mr32(address, PSPA_PHYSTATE10_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[11] =
			pm8001_mr32(address, PSPA_PHYSTATE11_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[12] =
			pm8001_mr32(address, PSPA_PHYSTATE12_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[13] =
			pm8001_mr32(address, PSPA_PHYSTATE13_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[14] =
			pm8001_mr32(address, PSPA_PHYSTATE14_OFFSET);
	pm8001_ha->phy_attr_table.phystart1_16[15] =
			pm8001_mr32(address, PSPA_PHYSTATE15_OFFSET);

	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[0] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID0_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[1] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID1_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[2] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID2_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[3] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID3_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[4] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID4_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[5] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID5_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[6] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID6_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[7] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID7_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[8] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID8_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[9] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID9_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[10] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID10_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[11] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID11_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[12] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID12_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[13] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID13_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[14] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID14_OFFSET);
	pm8001_ha->phy_attr_table.outbound_hw_event_pid1_16[15] =
			pm8001_mr32(address, PSPA_OB_HW_EVENT_PID15_OFFSET);

}

/**
 * read_inbnd_queue_table - read the inbound queue table and save it.
 * @pm8001_ha: our hba card information
 */
static void read_inbnd_queue_table(struct pm8001_hba_info *pm8001_ha)
{
	int i;
	void __iomem *address = pm8001_ha->inbnd_q_tbl_addr;
	for (i = 0; i < PM8001_MAX_INB_NUM; i++) {
		u32 offset = i * 0x20;
		pm8001_ha->inbnd_q_tbl[i].pi_pci_bar =
			get_pci_bar_index(pm8001_mr32(address,
				(offset + IB_PIPCI_BAR)));
		pm8001_ha->inbnd_q_tbl[i].pi_offset =
			pm8001_mr32(address, (offset + IB_PIPCI_BAR_OFFSET));
	}
}

/**
 * read_outbnd_queue_table - read the outbound queue table and save it.
 * @pm8001_ha: our hba card information
 */
static void read_outbnd_queue_table(struct pm8001_hba_info *pm8001_ha)
{
	int i;
	void __iomem *address = pm8001_ha->outbnd_q_tbl_addr;
	for (i = 0; i < PM8001_MAX_OUTB_NUM; i++) {
		u32 offset = i * 0x24;
		pm8001_ha->outbnd_q_tbl[i].ci_pci_bar =
			get_pci_bar_index(pm8001_mr32(address,
				(offset + OB_CIPCI_BAR)));
		pm8001_ha->outbnd_q_tbl[i].ci_offset =
			pm8001_mr32(address, (offset + OB_CIPCI_BAR_OFFSET));
	}
}

/**
 * init_default_table_values - init the default table.
 * @pm8001_ha: our hba card information
 */
static void init_default_table_values(struct pm8001_hba_info *pm8001_ha)
{
	int i;
	u32 offsetib, offsetob;
	void __iomem *addressib = pm8001_ha->inbnd_q_tbl_addr;
	void __iomem *addressob = pm8001_ha->outbnd_q_tbl_addr;
	u32 ib_offset = pm8001_ha->ib_offset;
	u32 ob_offset = pm8001_ha->ob_offset;
	u32 ci_offset = pm8001_ha->ci_offset;
	u32 pi_offset = pm8001_ha->pi_offset;

	pm8001_ha->main_cfg_tbl.pm80xx_tbl.upper_event_log_addr		=
		pm8001_ha->memoryMap.region[AAP1].phys_addr_hi;
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.lower_event_log_addr		=
		pm8001_ha->memoryMap.region[AAP1].phys_addr_lo;
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.event_log_size		=
							PM8001_EVENT_LOG_SIZE;
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.event_log_severity		= 0x01;
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.upper_pcs_event_log_addr	=
		pm8001_ha->memoryMap.region[IOP].phys_addr_hi;
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.lower_pcs_event_log_addr	=
		pm8001_ha->memoryMap.region[IOP].phys_addr_lo;
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.pcs_event_log_size		=
							PM8001_EVENT_LOG_SIZE;
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.pcs_event_log_severity	= 0x01;
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.fatal_err_interrupt		= 0x01;

	/* Disable end to end CRC checking */
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.crc_core_dump = (0x1 << 16);

	for (i = 0; i < pm8001_ha->max_q_num; i++) {
		pm8001_ha->inbnd_q_tbl[i].element_pri_size_cnt	=
			PM8001_MPI_QUEUE | (pm8001_ha->iomb_size << 16) | (0x00<<30);
		pm8001_ha->inbnd_q_tbl[i].upper_base_addr	=
			pm8001_ha->memoryMap.region[ib_offset + i].phys_addr_hi;
		pm8001_ha->inbnd_q_tbl[i].lower_base_addr	=
		pm8001_ha->memoryMap.region[ib_offset + i].phys_addr_lo;
		pm8001_ha->inbnd_q_tbl[i].base_virt		=
		  (u8 *)pm8001_ha->memoryMap.region[ib_offset + i].virt_ptr;
		pm8001_ha->inbnd_q_tbl[i].total_length		=
			pm8001_ha->memoryMap.region[ib_offset + i].total_len;
		pm8001_ha->inbnd_q_tbl[i].ci_upper_base_addr	=
			pm8001_ha->memoryMap.region[ci_offset + i].phys_addr_hi;
		pm8001_ha->inbnd_q_tbl[i].ci_lower_base_addr	=
			pm8001_ha->memoryMap.region[ci_offset + i].phys_addr_lo;
		pm8001_ha->inbnd_q_tbl[i].ci_virt		=
			pm8001_ha->memoryMap.region[ci_offset + i].virt_ptr;
		pm8001_write_32(pm8001_ha->inbnd_q_tbl[i].ci_virt, 0, 0);
		offsetib = i * 0x20;
		pm8001_ha->inbnd_q_tbl[i].pi_pci_bar		=
			get_pci_bar_index(pm8001_mr32(addressib,
				(offsetib + 0x14)));
		pm8001_ha->inbnd_q_tbl[i].pi_offset		=
			pm8001_mr32(addressib, (offsetib + 0x18));
		pm8001_ha->inbnd_q_tbl[i].producer_idx		= 0;
		pm8001_ha->inbnd_q_tbl[i].consumer_index	= 0;

		pm8001_dbg(pm8001_ha, DEV,
			   "IQ %d pi_bar 0x%x pi_offset 0x%x\n", i,
			   pm8001_ha->inbnd_q_tbl[i].pi_pci_bar,
			   pm8001_ha->inbnd_q_tbl[i].pi_offset);
	}
	for (i = 0; i < pm8001_ha->max_q_num; i++) {
		pm8001_ha->outbnd_q_tbl[i].element_size_cnt	=
			PM8001_MPI_QUEUE | (pm8001_ha->iomb_size << 16) | (0x01<<30);
		pm8001_ha->outbnd_q_tbl[i].upper_base_addr	=
			pm8001_ha->memoryMap.region[ob_offset + i].phys_addr_hi;
		pm8001_ha->outbnd_q_tbl[i].lower_base_addr	=
			pm8001_ha->memoryMap.region[ob_offset + i].phys_addr_lo;
		pm8001_ha->outbnd_q_tbl[i].base_virt		=
		  (u8 *)pm8001_ha->memoryMap.region[ob_offset + i].virt_ptr;
		pm8001_ha->outbnd_q_tbl[i].total_length		=
			pm8001_ha->memoryMap.region[ob_offset + i].total_len;
		pm8001_ha->outbnd_q_tbl[i].pi_upper_base_addr	=
			pm8001_ha->memoryMap.region[pi_offset + i].phys_addr_hi;
		pm8001_ha->outbnd_q_tbl[i].pi_lower_base_addr	=
			pm8001_ha->memoryMap.region[pi_offset + i].phys_addr_lo;
		/* interrupt vector based on oq */
		pm8001_ha->outbnd_q_tbl[i].interrup_vec_cnt_delay = (i << 24);
		pm8001_ha->outbnd_q_tbl[i].pi_virt		=
			pm8001_ha->memoryMap.region[pi_offset + i].virt_ptr;
		pm8001_write_32(pm8001_ha->outbnd_q_tbl[i].pi_virt, 0, 0);
		offsetob = i * 0x24;
		pm8001_ha->outbnd_q_tbl[i].ci_pci_bar		=
			get_pci_bar_index(pm8001_mr32(addressob,
			offsetob + 0x14));
		pm8001_ha->outbnd_q_tbl[i].ci_offset		=
			pm8001_mr32(addressob, (offsetob + 0x18));
		pm8001_ha->outbnd_q_tbl[i].consumer_idx		= 0;
		pm8001_ha->outbnd_q_tbl[i].producer_index	= 0;

		pm8001_dbg(pm8001_ha, DEV,
			   "OQ %d ci_bar 0x%x ci_offset 0x%x\n", i,
			   pm8001_ha->outbnd_q_tbl[i].ci_pci_bar,
			   pm8001_ha->outbnd_q_tbl[i].ci_offset);
	}
}

/**
 * update_main_config_table - update the main default table to the HBA.
 * @pm8001_ha: our hba card information
 */
static void update_main_config_table(struct pm8001_hba_info *pm8001_ha)
{
	void __iomem *address = pm8001_ha->main_cfg_tbl_addr;
	pm8001_mw32(address, MAIN_IQNPPD_HPPD_OFFSET,
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.inbound_q_nppd_hppd);
	pm8001_mw32(address, MAIN_EVENT_LOG_ADDR_HI,
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.upper_event_log_addr);
	pm8001_mw32(address, MAIN_EVENT_LOG_ADDR_LO,
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.lower_event_log_addr);
	pm8001_mw32(address, MAIN_EVENT_LOG_BUFF_SIZE,
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.event_log_size);
	pm8001_mw32(address, MAIN_EVENT_LOG_OPTION,
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.event_log_severity);
	pm8001_mw32(address, MAIN_PCS_EVENT_LOG_ADDR_HI,
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.upper_pcs_event_log_addr);
	pm8001_mw32(address, MAIN_PCS_EVENT_LOG_ADDR_LO,
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.lower_pcs_event_log_addr);
	pm8001_mw32(address, MAIN_PCS_EVENT_LOG_BUFF_SIZE,
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.pcs_event_log_size);
	pm8001_mw32(address, MAIN_PCS_EVENT_LOG_OPTION,
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.pcs_event_log_severity);
	/* Update Fatal error interrupt vector */
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.fatal_err_interrupt |=
					((pm8001_ha->max_q_num - 1) << 8);
	pm8001_mw32(address, MAIN_FATAL_ERROR_INTERRUPT,
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.fatal_err_interrupt);
	pm8001_dbg(pm8001_ha, DEV,
		   "Updated Fatal error interrupt vector 0x%x\n",
		   pm8001_mr32(address, MAIN_FATAL_ERROR_INTERRUPT));

	pm8001_mw32(address, MAIN_EVENT_CRC_CHECK,
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.crc_core_dump);

	/* SPCv specific */
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.gpio_led_mapping &= 0xCFFFFFFF;
	/* Set GPIOLED to 0x2 for LED indicator */
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.gpio_led_mapping |= 0x20000000;
	pm8001_mw32(address, MAIN_GPIO_LED_FLAGS_OFFSET,
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.gpio_led_mapping);
	pm8001_dbg(pm8001_ha, DEV,
		   "Programming DW 0x21 in main cfg table with 0x%x\n",
		   pm8001_mr32(address, MAIN_GPIO_LED_FLAGS_OFFSET));

	pm8001_mw32(address, MAIN_PORT_RECOVERY_TIMER,
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.port_recovery_timer);
	pm8001_mw32(address, MAIN_INT_REASSERTION_DELAY,
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.interrupt_reassertion_delay);

	pm8001_ha->main_cfg_tbl.pm80xx_tbl.port_recovery_timer &= 0xffff0000;
	pm8001_ha->main_cfg_tbl.pm80xx_tbl.port_recovery_timer |=
							PORT_RECOVERY_TIMEOUT;
	if (pm8001_ha->chip_id == chip_8006) {
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.port_recovery_timer &=
					0x0000ffff;
		pm8001_ha->main_cfg_tbl.pm80xx_tbl.port_recovery_timer |=
					CHIP_8006_PORT_RECOVERY_TIMEOUT;
	}
	pm8001_mw32(address, MAIN_PORT_RECOVERY_TIMER,
			pm8001_ha->main_cfg_tbl.pm80xx_tbl.port_recovery_timer);
}

/**
 * update_inbnd_queue_table - update the inbound queue table to the HBA.
 * @pm8001_ha: our hba card information
 * @number: entry in the queue
 */
static void update_inbnd_queue_table(struct pm8001_hba_info *pm8001_ha,
					 int number)
{
	void __iomem *address = pm8001_ha->inbnd_q_tbl_addr;
	u16 offset = number * 0x20;
	pm8001_mw32(address, offset + IB_PROPERITY_OFFSET,
		pm8001_ha->inbnd_q_tbl[number].element_pri_size_cnt);
	pm8001_mw32(address, offset + IB_BASE_ADDR_HI_OFFSET,
		pm8001_ha->inbnd_q_tbl[number].upper_base_addr);
	pm8001_mw32(address, offset + IB_BASE_ADDR_LO_OFFSET,
		pm8001_ha->inbnd_q_tbl[number].lower_base_addr);
	pm8001_mw32(address, offset + IB_CI_BASE_ADDR_HI_OFFSET,
		pm8001_ha->inbnd_q_tbl[number].ci_upper_base_addr);
	pm8001_mw32(address, offset + IB_CI_BASE_ADDR_LO_OFFSET,
		pm8001_ha->inbnd_q_tbl[number].ci_lower_base_addr);

	pm8001_dbg(pm8001_ha, DEV,
		   "IQ %d: Element pri size 0x%x\n",
		   number,
		   pm8001_ha->inbnd_q_tbl[number].element_pri_size_cnt);

	pm8001_dbg(pm8001_ha, DEV,
		   "IQ upr base addr 0x%x IQ lwr base addr 0x%x\n",
		   pm8001_ha->inbnd_q_tbl[number].upper_base_addr,
		   pm8001_ha->inbnd_q_tbl[number].lower_base_addr);

	pm8001_dbg(pm8001_ha, DEV,
		   "CI upper base addr 0x%x CI lower base addr 0x%x\n",
		   pm8001_ha->inbnd_q_tbl[number].ci_upper_base_addr,
		   pm8001_ha->inbnd_q_tbl[number].ci_lower_base_addr);
}

/**
 * update_outbnd_queue_table - update the outbound queue table to the HBA.
 * @pm8001_ha: our hba card information
 * @number: entry in the queue
 */
static void update_outbnd_queue_table(struct pm8001_hba_info *pm8001_ha,
						 int number)
{
	void __iomem *address = pm8001_ha->outbnd_q_tbl_addr;
	u16 offset = number * 0x24;
	pm8001_mw32(address, offset + OB_PROPERITY_OFFSET,
		pm8001_ha->outbnd_q_tbl[number].element_size_cnt);
	pm8001_mw32(address, offset + OB_BASE_ADDR_HI_OFFSET,
		pm8001_ha->outbnd_q_tbl[number].upper_base_addr);
	pm8001_mw32(address, offset + OB_BASE_ADDR_LO_OFFSET,
		pm8001_ha->outbnd_q_tbl[number].lower_base_addr);
	pm8001_mw32(address, offset + OB_PI_BASE_ADDR_HI_OFFSET,
		pm8001_ha->outbnd_q_tbl[number].pi_upper_base_addr);
	pm8001_mw32(address, offset + OB_PI_BASE_ADDR_LO_OFFSET,
		pm8001_ha->outbnd_q_tbl[number].pi_lower_base_addr);
	pm8001_mw32(address, offset + OB_INTERRUPT_COALES_OFFSET,
		pm8001_ha->outbnd_q_tbl[number].interrup_vec_cnt_delay);

	pm8001_dbg(pm8001_ha, DEV,
		   "OQ %d: Element pri size 0x%x\n",
		   number,
		   pm8001_ha->outbnd_q_tbl[number].element_size_cnt);

	pm8001_dbg(pm8001_ha, DEV,
		   "OQ upr base addr 0x%x OQ lwr base addr 0x%x\n",
		   pm8001_ha->outbnd_q_tbl[number].upper_base_addr,
		   pm8001_ha->outbnd_q_tbl[number].lower_base_addr);

	pm8001_dbg(pm8001_ha, DEV,
		   "PI upper base addr 0x%x PI lower base addr 0x%x\n",
		   pm8001_ha->outbnd_q_tbl[number].pi_upper_base_addr,
		   pm8001_ha->outbnd_q_tbl[number].pi_lower_base_addr);
}

/**
 * mpi_init_check - check firmware initialization status.
 * @pm8001_ha: our hba card information
 */
static int mpi_init_check(struct pm8001_hba_info *pm8001_ha)
{
	u32 max_wait_count;
	u32 value;
	u32 gst_len_mpistate;

	/* Write bit0=1 to Inbound DoorBell Register to tell the SPC FW the
	table is updated */
	pm8001_cw32(pm8001_ha, 0, MSGU_IBDB_SET, SPCv_MSGU_CFG_TABLE_UPDATE);
	/* wait until Inbound DoorBell Clear Register toggled */
	if (IS_SPCV_12G(pm8001_ha->pdev)) {
		max_wait_count = SPCV_DOORBELL_CLEAR_TIMEOUT;
	} else {
		max_wait_count = SPC_DOORBELL_CLEAR_TIMEOUT;
	}
	do {
		msleep(FW_READY_INTERVAL);
		value = pm8001_cr32(pm8001_ha, 0, MSGU_IBDB_SET);
		value &= SPCv_MSGU_CFG_TABLE_UPDATE;
	} while ((value != 0) && (--max_wait_count));

	if (!max_wait_count) {
		/* additional check */
		pm8001_dbg(pm8001_ha, FAIL,
			   "Inb doorbell clear not toggled[value:%x]\n",
			   value);
		return -EBUSY;
	}
	/* check the MPI-State for initialization up to 100ms*/
	max_wait_count = 5;/* 100 msec */
	do {
		msleep(FW_READY_INTERVAL);
		gst_len_mpistate =
			pm8001_mr32(pm8001_ha->general_stat_tbl_addr,
					GST_GSTLEN_MPIS_OFFSET);
	} while ((GST_MPI_STATE_INIT !=
		(gst_len_mpistate & GST_MPI_STATE_MASK)) && (--max_wait_count));
	if (!max_wait_count)
		return -EBUSY;

	/* check MPI Initialization error */
	gst_len_mpistate = gst_len_mpistate >> 16;
	if (0x0000 != gst_len_mpistate)
		return -EBUSY;

	return 0;
}

/**
 * check_fw_ready - The LLDD check if the FW is ready, if not, return error.
 * This function sleeps hence it must not be used in atomic context.
 * @pm8001_ha: our hba card information
 */
static int check_fw_ready(struct pm8001_hba_info *pm8001_ha)
{
	u32 value;
	u32 max_wait_count;
	u32 max_wait_time;
	u32 expected_mask;
	int ret = 0;

	/* reset / PCIe ready */
	max_wait_time = max_wait_count = 5;	/* 100 milli sec */
	do {
		msleep(FW_READY_INTERVAL);
		value = pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_1);
	} while ((value == 0xFFFFFFFF) && (--max_wait_count));

	/* check ila, RAAE and iops status */
	if ((pm8001_ha->chip_id != chip_8008) &&
			(pm8001_ha->chip_id != chip_8009)) {
		max_wait_time = max_wait_count = 180;   /* 3600 milli sec */
		expected_mask = SCRATCH_PAD_ILA_READY |
			SCRATCH_PAD_RAAE_READY |
			SCRATCH_PAD_IOP0_READY |
			SCRATCH_PAD_IOP1_READY;
	} else {
		max_wait_time = max_wait_count = 170;   /* 3400 milli sec */
		expected_mask = SCRATCH_PAD_ILA_READY |
			SCRATCH_PAD_RAAE_READY |
			SCRATCH_PAD_IOP0_READY;
	}
	do {
		msleep(FW_READY_INTERVAL);
		value = pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_1);
	} while (((value & expected_mask) !=
				 expected_mask) && (--max_wait_count));
	if (!max_wait_count) {
		pm8001_dbg(pm8001_ha, INIT,
		"At least one FW component failed to load within %d millisec: Scratchpad1: 0x%x\n",
			max_wait_time * FW_READY_INTERVAL, value);
		ret = -1;
	} else {
		pm8001_dbg(pm8001_ha, MSG,
			"All FW components ready by %d ms\n",
			(max_wait_time - max_wait_count) * FW_READY_INTERVAL);
	}
	return ret;
}

static int init_pci_device_addresses(struct pm8001_hba_info *pm8001_ha)
{
	void __iomem *base_addr;
	u32	value;
	u32	offset;
	u32	pcibar;
	u32	pcilogic;

	value = pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_0);

	/*
	 * lower 26 bits of SCRATCHPAD0 register describes offset within the
	 * PCIe BAR where the MPI configuration table is present
	 */
	offset = value & 0x03FFFFFF; /* scratch pad 0 TBL address */

	pm8001_dbg(pm8001_ha, DEV, "Scratchpad 0 Offset: 0x%x value 0x%x\n",
		   offset, value);
	/*
	 * Upper 6 bits describe the offset within PCI config space where BAR
	 * is located.
	 */
	pcilogic = (value & 0xFC000000) >> 26;
	pcibar = get_pci_bar_index(pcilogic);
	pm8001_dbg(pm8001_ha, INIT, "Scratchpad 0 PCI BAR: %d\n", pcibar);

	/*
	 * Make sure the offset falls inside the ioremapped PCI BAR
	 */
	if (offset > pm8001_ha->io_mem[pcibar].memsize) {
		pm8001_dbg(pm8001_ha, FAIL,
			"Main cfg tbl offset outside %u > %u\n",
				offset, pm8001_ha->io_mem[pcibar].memsize);
		return -EBUSY;
	}
	pm8001_ha->main_cfg_tbl_addr = base_addr =
		pm8001_ha->io_mem[pcibar].memvirtaddr + offset;

	/*
	 * Validate main configuration table address: first DWord should read
	 * "PMCS"
	 */
	value = pm8001_mr32(pm8001_ha->main_cfg_tbl_addr, 0);
	if (memcmp(&value, "PMCS", 4) != 0) {
		pm8001_dbg(pm8001_ha, FAIL,
			"BAD main config signature 0x%x\n",
				value);
		return -EBUSY;
	}
	pm8001_dbg(pm8001_ha, INIT,
			"VALID main config signature 0x%x\n", value);
	pm8001_ha->general_stat_tbl_addr =
		base_addr + (pm8001_cr32(pm8001_ha, pcibar, offset + 0x18) &
					0xFFFFFF);
	pm8001_ha->inbnd_q_tbl_addr =
		base_addr + (pm8001_cr32(pm8001_ha, pcibar, offset + 0x1C) &
					0xFFFFFF);
	pm8001_ha->outbnd_q_tbl_addr =
		base_addr + (pm8001_cr32(pm8001_ha, pcibar, offset + 0x20) &
					0xFFFFFF);
	pm8001_ha->ivt_tbl_addr =
		base_addr + (pm8001_cr32(pm8001_ha, pcibar, offset + 0x8C) &
					0xFFFFFF);
	pm8001_ha->pspa_q_tbl_addr =
		base_addr + (pm8001_cr32(pm8001_ha, pcibar, offset + 0x90) &
					0xFFFFFF);
	pm8001_ha->fatal_tbl_addr =
		base_addr + (pm8001_cr32(pm8001_ha, pcibar, offset + 0xA0) &
					0xFFFFFF);

	pm8001_dbg(pm8001_ha, INIT, "GST OFFSET 0x%x\n",
		   pm8001_cr32(pm8001_ha, pcibar, offset + 0x18));
	pm8001_dbg(pm8001_ha, INIT, "INBND OFFSET 0x%x\n",
		   pm8001_cr32(pm8001_ha, pcibar, offset + 0x1C));
	pm8001_dbg(pm8001_ha, INIT, "OBND OFFSET 0x%x\n",
		   pm8001_cr32(pm8001_ha, pcibar, offset + 0x20));
	pm8001_dbg(pm8001_ha, INIT, "IVT OFFSET 0x%x\n",
		   pm8001_cr32(pm8001_ha, pcibar, offset + 0x8C));
	pm8001_dbg(pm8001_ha, INIT, "PSPA OFFSET 0x%x\n",
		   pm8001_cr32(pm8001_ha, pcibar, offset + 0x90));
	pm8001_dbg(pm8001_ha, INIT, "addr - main cfg %p general status %p\n",
		   pm8001_ha->main_cfg_tbl_addr,
		   pm8001_ha->general_stat_tbl_addr);
	pm8001_dbg(pm8001_ha, INIT, "addr - inbnd %p obnd %p\n",
		   pm8001_ha->inbnd_q_tbl_addr,
		   pm8001_ha->outbnd_q_tbl_addr);
	pm8001_dbg(pm8001_ha, INIT, "addr - pspa %p ivt %p\n",
		   pm8001_ha->pspa_q_tbl_addr,
		   pm8001_ha->ivt_tbl_addr);
	return 0;
}

/**
 * pm80xx_set_thermal_config - support the thermal configuration
 * @pm8001_ha: our hba card information.
 */
int
pm80xx_set_thermal_config(struct pm8001_hba_info *pm8001_ha)
{
	struct set_ctrl_cfg_req payload;
	struct inbound_queue_table *circularQ;
	int rc;
	u32 tag;
	u32 opc = OPC_INB_SET_CONTROLLER_CONFIG;
	u32 page_code;

	memset(&payload, 0, sizeof(struct set_ctrl_cfg_req));
	rc = pm8001_tag_alloc(pm8001_ha, &tag);
	if (rc)
		return -1;

	circularQ = &pm8001_ha->inbnd_q_tbl[0];
	payload.tag = cpu_to_le32(tag);

	if (IS_SPCV_12G(pm8001_ha->pdev))
		page_code = THERMAL_PAGE_CODE_7H;
	else
		page_code = THERMAL_PAGE_CODE_8H;

	payload.cfg_pg[0] = (THERMAL_LOG_ENABLE << 9) |
				(THERMAL_ENABLE << 8) | page_code;
	payload.cfg_pg[1] = (LTEMPHIL << 24) | (RTEMPHIL << 8);

	pm8001_dbg(pm8001_ha, DEV,
		   "Setting up thermal config. cfg_pg 0 0x%x cfg_pg 1 0x%x\n",
		   payload.cfg_pg[0], payload.cfg_pg[1]);

	rc = pm8001_mpi_build_cmd(pm8001_ha, circularQ, opc, &payload,
			sizeof(payload), 0);
	if (rc)
		pm8001_tag_free(pm8001_ha, tag);
	return rc;

}

/**
* pm80xx_set_sas_protocol_timer_config - support the SAS Protocol
* Timer configuration page
* @pm8001_ha: our hba card information.
*/
static int
pm80xx_set_sas_protocol_timer_config(struct pm8001_hba_info *pm8001_ha)
{
	struct set_ctrl_cfg_req payload;
	struct inbound_queue_table *circularQ;
	SASProtocolTimerConfig_t SASConfigPage;
	int rc;
	u32 tag;
	u32 opc = OPC_INB_SET_CONTROLLER_CONFIG;

	memset(&payload, 0, sizeof(struct set_ctrl_cfg_req));
	memset(&SASConfigPage, 0, sizeof(SASProtocolTimerConfig_t));

	rc = pm8001_tag_alloc(pm8001_ha, &tag);

	if (rc)
		return -1;

	circularQ = &pm8001_ha->inbnd_q_tbl[0];
	payload.tag = cpu_to_le32(tag);

	SASConfigPage.pageCode        =  SAS_PROTOCOL_TIMER_CONFIG_PAGE;
	SASConfigPage.MST_MSI         =  3 << 15;
	SASConfigPage.STP_SSP_MCT_TMO =  (STP_MCT_TMO << 16) | SSP_MCT_TMO;
	SASConfigPage.STP_FRM_TMO     = (SAS_MAX_OPEN_TIME << 24) |
				(SMP_MAX_CONN_TIMER << 16) | STP_FRM_TIMER;
	SASConfigPage.STP_IDLE_TMO    =  STP_IDLE_TIME;

	if (SASConfigPage.STP_IDLE_TMO > 0x3FFFFFF)
		SASConfigPage.STP_IDLE_TMO = 0x3FFFFFF;


	SASConfigPage.OPNRJT_RTRY_INTVL =         (SAS_MFD << 16) |
						SAS_OPNRJT_RTRY_INTVL;
	SASConfigPage.Data_Cmd_OPNRJT_RTRY_TMO =  (SAS_DOPNRJT_RTRY_TMO << 16)
						| SAS_COPNRJT_RTRY_TMO;
	SASConfigPage.Data_Cmd_OPNRJT_RTRY_THR =  (SAS_DOPNRJT_RTRY_THR << 16)
						| SAS_COPNRJT_RTRY_THR;
	SASConfigPage.MAX_AIP =  SAS_MAX_AIP;

	pm8001_dbg(pm8001_ha, INIT, "SASConfigPage.pageCode 0x%08x\n",
		   SASConfigPage.pageCode);
	pm8001_dbg(pm8001_ha, INIT, "SASConfigPage.MST_MSI  0x%08x\n",
		   SASConfigPage.MST_MSI);
	pm8001_dbg(pm8001_ha, INIT, "SASConfigPage.STP_SSP_MCT_TMO  0x%08x\n",
		   SASConfigPage.STP_SSP_MCT_TMO);
	pm8001_dbg(pm8001_ha, INIT, "SASConfigPage.STP_FRM_TMO  0x%08x\n",
		   SASConfigPage.STP_FRM_TMO);
	pm8001_dbg(pm8001_ha, INIT, "SASConfigPage.STP_IDLE_TMO  0x%08x\n",
		   SASConfigPage.STP_IDLE_TMO);
	pm8001_dbg(pm8001_ha, INIT, "SASConfigPage.OPNRJT_RTRY_INTVL  0x%08x\n",
		   SASConfigPage.OPNRJT_RTRY_INTVL);
	pm8001_dbg(pm8001_ha, INIT, "SASConfigPage.Data_Cmd_OPNRJT_RTRY_TMO  0x%08x\n",
		   SASConfigPage.Data_Cmd_OPNRJT_RTRY_TMO);
	pm8001_dbg(pm8001_ha, INIT, "SASConfigPage.Data_Cmd_OPNRJT_RTRY_THR  0x%08x\n",
		   SASConfigPage.Data_Cmd_OPNRJT_RTRY_THR);
	pm8001_dbg(pm8001_ha, INIT, "SASConfigPage.MAX_AIP  0x%08x\n",
		   SASConfigPage.MAX_AIP);

	memcpy(&payload.cfg_pg, &SASConfigPage,
			 sizeof(SASProtocolTimerConfig_t));

	rc = pm8001_mpi_build_cmd(pm8001_ha, circularQ, opc, &payload,
			sizeof(payload), 0);
	if (rc)
		pm8001_tag_free(pm8001_ha, tag);

	return rc;
}

/**
 * pm80xx_get_encrypt_info - Check for encryption
 * @pm8001_ha: our hba card information.
 */
static int
pm80xx_get_encrypt_info(struct pm8001_hba_info *pm8001_ha)
{
	u32 scratch3_value;
	int ret = -1;

	/* Read encryption status from SCRATCH PAD 3 */
	scratch3_value = pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_3);

	if ((scratch3_value & SCRATCH_PAD3_ENC_MASK) ==
					SCRATCH_PAD3_ENC_READY) {
		if (scratch3_value & SCRATCH_PAD3_XTS_ENABLED)
			pm8001_ha->encrypt_info.cipher_mode = CIPHER_MODE_XTS;
		if ((scratch3_value & SCRATCH_PAD3_SM_MASK) ==
						SCRATCH_PAD3_SMF_ENABLED)
			pm8001_ha->encrypt_info.sec_mode = SEC_MODE_SMF;
		if ((scratch3_value & SCRATCH_PAD3_SM_MASK) ==
						SCRATCH_PAD3_SMA_ENABLED)
			pm8001_ha->encrypt_info.sec_mode = SEC_MODE_SMA;
		if ((scratch3_value & SCRATCH_PAD3_SM_MASK) ==
						SCRATCH_PAD3_SMB_ENABLED)
			pm8001_ha->encrypt_info.sec_mode = SEC_MODE_SMB;
		pm8001_ha->encrypt_info.status = 0;
		pm8001_dbg(pm8001_ha, INIT,
			   "Encryption: SCRATCH_PAD3_ENC_READY 0x%08X.Cipher mode 0x%x Sec mode 0x%x status 0x%x\n",
			   scratch3_value,
			   pm8001_ha->encrypt_info.cipher_mode,
			   pm8001_ha->encrypt_info.sec_mode,
			   pm8001_ha->encrypt_info.status);
		ret = 0;
	} else if ((scratch3_value & SCRATCH_PAD3_ENC_READY) ==
					SCRATCH_PAD3_ENC_DISABLED) {
		pm8001_dbg(pm8001_ha, INIT,
			   "Encryption: SCRATCH_PAD3_ENC_DISABLED 0x%08X\n",
			   scratch3_value);
		pm8001_ha->encrypt_info.status = 0xFFFFFFFF;
		pm8001_ha->encrypt_info.cipher_mode = 0;
		pm8001_ha->encrypt_info.sec_mode = 0;
		ret = 0;
	} else if ((scratch3_value & SCRATCH_PAD3_ENC_MASK) ==
				SCRATCH_PAD3_ENC_DIS_ERR) {
		pm8001_ha->encrypt_info.status =
			(scratch3_value & SCRATCH_PAD3_ERR_CODE) >> 16;
		if (scratch3_value & SCRATCH_PAD3_XTS_ENABLED)
			pm8001_ha->encrypt_info.cipher_mode = CIPHER_MODE_XTS;
		if ((scratch3_value & SCRATCH_PAD3_SM_MASK) ==
					SCRATCH_PAD3_SMF_ENABLED)
			pm8001_ha->encrypt_info.sec_mode = SEC_MODE_SMF;
		if ((scratch3_value & SCRATCH_PAD3_SM_MASK) ==
					SCRATCH_PAD3_SMA_ENABLED)
			pm8001_ha->encrypt_info.sec_mode = SEC_MODE_SMA;
		if ((scratch3_value & SCRATCH_PAD3_SM_MASK) ==
					SCRATCH_PAD3_SMB_ENABLED)
			pm8001_ha->encrypt_info.sec_mode = SEC_MODE_SMB;
		pm8001_dbg(pm8001_ha, INIT,
			   "Encryption: SCRATCH_PAD3_DIS_ERR 0x%08X.Cipher mode 0x%x sec mode 0x%x status 0x%x\n",
			   scratch3_value,
			   pm8001_ha->encrypt_info.cipher_mode,
			   pm8001_ha->encrypt_info.sec_mode,
			   pm8001_ha->encrypt_info.status);
	} else if ((scratch3_value & SCRATCH_PAD3_ENC_MASK) ==
				 SCRATCH_PAD3_ENC_ENA_ERR) {

		pm8001_ha->encrypt_info.status =
			(scratch3_value & SCRATCH_PAD3_ERR_CODE) >> 16;
		if (scratch3_value & SCRATCH_PAD3_XTS_ENABLED)
			pm8001_ha->encrypt_info.cipher_mode = CIPHER_MODE_XTS;
		if ((scratch3_value & SCRATCH_PAD3_SM_MASK) ==
					SCRATCH_PAD3_SMF_ENABLED)
			pm8001_ha->encrypt_info.sec_mode = SEC_MODE_SMF;
		if ((scratch3_value & SCRATCH_PAD3_SM_MASK) ==
					SCRATCH_PAD3_SMA_ENABLED)
			pm8001_ha->encrypt_info.sec_mode = SEC_MODE_SMA;
		if ((scratch3_value & SCRATCH_PAD3_SM_MASK) ==
					SCRATCH_PAD3_SMB_ENABLED)
			pm8001_ha->encrypt_info.sec_mode = SEC_MODE_SMB;

		pm8001_dbg(pm8001_ha, INIT,
			   "Encryption: SCRATCH_PAD3_ENA_ERR 0x%08X.Cipher mode 0x%x sec mode 0x%x status 0x%x\n",
			   scratch3_value,
			   pm8001_ha->encrypt_info.cipher_mode,
			   pm8001_ha->encrypt_info.sec_mode,
			   pm8001_ha->encrypt_info.status);
	}
	return ret;
}

/**
 * pm80xx_encrypt_update - update flash with encryption information
 * @pm8001_ha: our hba card information.
 */
static int pm80xx_encrypt_update(struct pm8001_hba_info *pm8001_ha)
{
	struct kek_mgmt_req payload;
	struct inbound_queue_table *circularQ;
	int rc;
	u32 tag;
	u32 opc = OPC_INB_KEK_MANAGEMENT;

	memset(&payload, 0, sizeof(struct kek_mgmt_req));
	rc = pm8001_tag_alloc(pm8001_ha, &tag);
	if (rc)
		return -1;

	circularQ = &pm8001_ha->inbnd_q_tbl[0];
	payload.tag = cpu_to_le32(tag);
	/* Currently only one key is used. New KEK index is 1.
	 * Current KEK index is 1. Store KEK to NVRAM is 1.
	 */
	payload.new_curidx_ksop = ((1 << 24) | (1 << 16) | (1 << 8) |
					KEK_MGMT_SUBOP_KEYCARDUPDATE);

	pm8001_dbg(pm8001_ha, DEV,
		   "Saving Encryption info to flash. payload 0x%x\n",
		   payload.new_curidx_ksop);

	rc = pm8001_mpi_build_cmd(pm8001_ha, circularQ, opc, &payload,
			sizeof(payload), 0);
	if (rc)
		pm8001_tag_free(pm8001_ha, tag);

	return rc;
}

/**
 * pm80xx_chip_init - the main init function that initializes whole PM8001 chip.
 * @pm8001_ha: our hba card information
 */
static int pm80xx_chip_init(struct pm8001_hba_info *pm8001_ha)
{
	int ret;
	u8 i = 0;

	/* check the firmware status */
	if (-1 == check_fw_ready(pm8001_ha)) {
		pm8001_dbg(pm8001_ha, FAIL, "Firmware is not ready!\n");
		return -EBUSY;
	}

	/* Initialize the controller fatal error flag */
	pm8001_ha->controller_fatal_error = false;

	/* Initialize pci space address eg: mpi offset */
	ret = init_pci_device_addresses(pm8001_ha);
	if (ret) {
		pm8001_dbg(pm8001_ha, FAIL,
			"Failed to init pci addresses");
		return ret;
	}
	init_default_table_values(pm8001_ha);
	read_main_config_table(pm8001_ha);
	read_general_status_table(pm8001_ha);
	read_inbnd_queue_table(pm8001_ha);
	read_outbnd_queue_table(pm8001_ha);
	read_phy_attr_table(pm8001_ha);

	/* update main config table ,inbound table and outbound table */
	update_main_config_table(pm8001_ha);
	for (i = 0; i < pm8001_ha->max_q_num; i++) {
		update_inbnd_queue_table(pm8001_ha, i);
		update_outbnd_queue_table(pm8001_ha, i);
	}
	/* notify firmware update finished and check initialization status */
	if (0 == mpi_init_check(pm8001_ha)) {
		pm8001_dbg(pm8001_ha, INIT, "MPI initialize successful!\n");
	} else
		return -EBUSY;

	/* send SAS protocol timer configuration page to FW */
	ret = pm80xx_set_sas_protocol_timer_config(pm8001_ha);

	/* Check for encryption */
	if (pm8001_ha->chip->encrypt) {
		pm8001_dbg(pm8001_ha, INIT, "Checking for encryption\n");
		ret = pm80xx_get_encrypt_info(pm8001_ha);
		if (ret == -1) {
			pm8001_dbg(pm8001_ha, INIT, "Encryption error !!\n");
			if (pm8001_ha->encrypt_info.status == 0x81) {
				pm8001_dbg(pm8001_ha, INIT,
					   "Encryption enabled with error.Saving encryption key to flash\n");
				pm80xx_encrypt_update(pm8001_ha);
			}
		}
	}
	return 0;
}

static int mpi_uninit_check(struct pm8001_hba_info *pm8001_ha)
{
	u32 max_wait_count;
	u32 value;
	u32 gst_len_mpistate;
	int ret;

	ret = init_pci_device_addresses(pm8001_ha);
	if (ret) {
		pm8001_dbg(pm8001_ha, FAIL,
			"Failed to init pci addresses");
		return ret;
	}

	/* Write bit1=1 to Inbound DoorBell Register to tell the SPC FW the
	table is stop */
	pm8001_cw32(pm8001_ha, 0, MSGU_IBDB_SET, SPCv_MSGU_CFG_TABLE_RESET);

	/* wait until Inbound DoorBell Clear Register toggled */
	if (IS_SPCV_12G(pm8001_ha->pdev)) {
		max_wait_count = SPCV_DOORBELL_CLEAR_TIMEOUT;
	} else {
		max_wait_count = SPC_DOORBELL_CLEAR_TIMEOUT;
	}
	do {
		msleep(FW_READY_INTERVAL);
		value = pm8001_cr32(pm8001_ha, 0, MSGU_IBDB_SET);
		value &= SPCv_MSGU_CFG_TABLE_RESET;
	} while ((value != 0) && (--max_wait_count));

	if (!max_wait_count) {
		pm8001_dbg(pm8001_ha, FAIL, "TIMEOUT:IBDB value/=%x\n", value);
		return -1;
	}

	/* check the MPI-State for termination in progress */
	/* wait until Inbound DoorBell Clear Register toggled */
	max_wait_count = 100; /* 2 sec for spcv/ve */
	do {
		msleep(FW_READY_INTERVAL);
		gst_len_mpistate =
			pm8001_mr32(pm8001_ha->general_stat_tbl_addr,
			GST_GSTLEN_MPIS_OFFSET);
		if (GST_MPI_STATE_UNINIT ==
			(gst_len_mpistate & GST_MPI_STATE_MASK))
			break;
	} while (--max_wait_count);
	if (!max_wait_count) {
		pm8001_dbg(pm8001_ha, FAIL, " TIME OUT MPI State = 0x%x\n",
			   gst_len_mpistate & GST_MPI_STATE_MASK);
		return -1;
	}

	return 0;
}

/**
 * pm80xx_fatal_errors - returns non-zero *ONLY* when fatal errors
 * @pm8001_ha: our hba card information
 *
 * Fatal errors are recoverable only after a host reboot.
 */
int
pm80xx_fatal_errors(struct pm8001_hba_info *pm8001_ha)
{
	int ret = 0;
	u32 scratch_pad_rsvd0 = pm8001_cr32(pm8001_ha, 0,
					MSGU_HOST_SCRATCH_PAD_6);
	u32 scratch_pad_rsvd1 = pm8001_cr32(pm8001_ha, 0,
					MSGU_HOST_SCRATCH_PAD_7);
	u32 scratch_pad1 = pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_1);
	u32 scratch_pad2 = pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_2);
	u32 scratch_pad3 = pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_3);

	if (pm8001_ha->chip_id != chip_8006 &&
			pm8001_ha->chip_id != chip_8074 &&
			pm8001_ha->chip_id != chip_8076) {
		return 0;
	}

	if (MSGU_SCRATCHPAD1_STATE_FATAL_ERROR(scratch_pad1)) {
		pm8001_dbg(pm8001_ha, FAIL,
			"Fatal error SCRATCHPAD1 = 0x%x SCRATCHPAD2 = 0x%x SCRATCHPAD3 = 0x%x SCRATCHPAD_RSVD0 = 0x%x SCRATCHPAD_RSVD1 = 0x%x\n",
				scratch_pad1, scratch_pad2, scratch_pad3,
				scratch_pad_rsvd0, scratch_pad_rsvd1);
		ret = 1;
	}

	return ret;
}

/**
 * pm80xx_chip_soft_rst - soft reset the PM8001 chip, so that all
 * FW register status are reset to the originated status.
 * @pm8001_ha: our hba card information
 */

static int
pm80xx_chip_soft_rst(struct pm8001_hba_info *pm8001_ha)
{
	u32 regval;
	u32 bootloader_state;
	u32 ibutton0, ibutton1;

	/* Process MPI table uninitialization only if FW is ready */
	if (!pm8001_ha->controller_fatal_error) {
		/* Check if MPI is in ready state to reset */
		if (mpi_uninit_check(pm8001_ha) != 0) {
			u32 r0 = pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_0);
			u32 r1 = pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_1);
			u32 r2 = pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_2);
			u32 r3 = pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_3);
			pm8001_dbg(pm8001_ha, FAIL,
				   "MPI state is not ready scratch: %x:%x:%x:%x\n",
				   r0, r1, r2, r3);
			/* if things aren't ready but the bootloader is ok then
			 * try the reset anyway.
			 */
			if (r1 & SCRATCH_PAD1_BOOTSTATE_MASK)
				return -1;
		}
	}
	/* checked for reset register normal state; 0x0 */
	regval = pm8001_cr32(pm8001_ha, 0, SPC_REG_SOFT_RESET);
	pm8001_dbg(pm8001_ha, INIT, "reset register before write : 0x%x\n",
		   regval);

	pm8001_cw32(pm8001_ha, 0, SPC_REG_SOFT_RESET, SPCv_NORMAL_RESET_VALUE);
	msleep(500);

	regval = pm8001_cr32(pm8001_ha, 0, SPC_REG_SOFT_RESET);
	pm8001_dbg(pm8001_ha, INIT, "reset register after write 0x%x\n",
		   regval);

	if ((regval & SPCv_SOFT_RESET_READ_MASK) ==
			SPCv_SOFT_RESET_NORMAL_RESET_OCCURED) {
		pm8001_dbg(pm8001_ha, MSG,
			   " soft reset successful [regval: 0x%x]\n",
			   regval);
	} else {
		pm8001_dbg(pm8001_ha, MSG,
			   " soft reset failed [regval: 0x%x]\n",
			   regval);

		/* check bootloader is successfully executed or in HDA mode */
		bootloader_state =
			pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_1) &
			SCRATCH_PAD1_BOOTSTATE_MASK;

		if (bootloader_state == SCRATCH_PAD1_BOOTSTATE_HDA_SEEPROM) {
			pm8001_dbg(pm8001_ha, MSG,
				   "Bootloader state - HDA mode SEEPROM\n");
		} else if (bootloader_state ==
				SCRATCH_PAD1_BOOTSTATE_HDA_BOOTSTRAP) {
			pm8001_dbg(pm8001_ha, MSG,
				   "Bootloader state - HDA mode Bootstrap Pin\n");
		} else if (bootloader_state ==
				SCRATCH_PAD1_BOOTSTATE_HDA_SOFTRESET) {
			pm8001_dbg(pm8001_ha, MSG,
				   "Bootloader state - HDA mode soft reset\n");
		} else if (bootloader_state ==
					SCRATCH_PAD1_BOOTSTATE_CRIT_ERROR) {
			pm8001_dbg(pm8001_ha, MSG,
				   "Bootloader state-HDA mode critical error\n");
		}
		return -EBUSY;
	}

	/* check the firmware status after reset */
	if (-1 == check_fw_ready(pm8001_ha)) {
		pm8001_dbg(pm8001_ha, FAIL, "Firmware is not ready!\n");
		/* check iButton feature support for motherboard controller */
		if (pm8001_ha->pdev->subsystem_vendor !=
			PCI_VENDOR_ID_ADAPTEC2 &&
			pm8001_ha->pdev->subsystem_vendor !=
			PCI_VENDOR_ID_ATTO &&
			pm8001_ha->pdev->subsystem_vendor != 0) {
			ibutton0 = pm8001_cr32(pm8001_ha, 0,
					MSGU_HOST_SCRATCH_PAD_6);
			ibutton1 = pm8001_cr32(pm8001_ha, 0,
					MSGU_HOST_SCRATCH_PAD_7);
			if (!ibutton0 && !ibutton1) {
				pm8001_dbg(pm8001_ha, FAIL,
					   "iButton Feature is not Available!!!\n");
				return -EBUSY;
			}
			if (ibutton0 == 0xdeadbeef && ibutton1 == 0xdeadbeef) {
				pm8001_dbg(pm8001_ha, FAIL,
					   "CRC Check for iButton Feature Failed!!!\n");
				return -EBUSY;
			}
		}
	}
	pm8001_dbg(pm8001_ha, INIT, "SPCv soft reset Complete\n");
	return 0;
}

static void pm80xx_hw_chip_rst(struct pm8001_hba_info *pm8001_ha)
{
	u32 i;

	pm8001_dbg(pm8001_ha, INIT, "chip reset start\n");

	/* do SPCv chip reset. */
	pm8001_cw32(pm8001_ha, 0, SPC_REG_SOFT_RESET, 0x11);
	pm8001_dbg(pm8001_ha, INIT, "SPC soft reset Complete\n");

	/* Check this ..whether delay is required or no */
	/* delay 10 usec */
	udelay(10);

	/* wait for 20 msec until the firmware gets reloaded */
	i = 20;
	do {
		mdelay(1);
	} while ((--i) != 0);

	pm8001_dbg(pm8001_ha, INIT, "chip reset finished\n");
}

/**
 * pm80xx_chip_intx_interrupt_enable - enable PM8001 chip interrupt
 * @pm8001_ha: our hba card information
 */
static void
pm80xx_chip_intx_interrupt_enable(struct pm8001_hba_info *pm8001_ha)
{
	pm8001_cw32(pm8001_ha, 0, MSGU_ODMR, ODMR_CLEAR_ALL);
	pm8001_cw32(pm8001_ha, 0, MSGU_ODCR, ODCR_CLEAR_ALL);
}

/**
 * pm80xx_chip_intx_interrupt_disable - disable PM8001 chip interrupt
 * @pm8001_ha: our hba card information
 */
static void
pm80xx_chip_intx_interrupt_disable(struct pm8001_hba_info *pm8001_ha)
{
	pm8001_cw32(pm8001_ha, 0, MSGU_ODMR_CLR, ODMR_MASK_ALL);
}

/**
 * pm80xx_chip_interrupt_enable - enable PM8001 chip interrupt
 * @pm8001_ha: our hba card information
 * @vec: interrupt number to enable
 */
static void
pm80xx_chip_interrupt_enable(struct pm8001_hba_info *pm8001_ha, u8 vec)
{
#ifdef PM8001_USE_MSIX
	u32 mask;
	mask = (u32)(1 << vec);

	pm8001_cw32(pm8001_ha, 0, MSGU_ODMR_CLR, (u32)(mask & 0xFFFFFFFF));
	return;
#endif
	pm80xx_chip_intx_interrupt_enable(pm8001_ha);

}

/**
 * pm80xx_chip_interrupt_disable - disable PM8001 chip interrupt
 * @pm8001_ha: our hba card information
 * @vec: interrupt number to disable
 */
static void
pm80xx_chip_interrupt_disable(struct pm8001_hba_info *pm8001_ha, u8 vec)
{
#ifdef PM8001_USE_MSIX
	u32 mask;
	if (vec == 0xFF)
		mask = 0xFFFFFFFF;
	else
		mask = (u32)(1 << vec);
	pm8001_cw32(pm8001_ha, 0, MSGU_ODMR, (u32)(mask & 0xFFFFFFFF));
	return;
#endif
	pm80xx_chip_intx_interrupt_disable(pm8001_ha);
}

static void pm80xx_send_abort_all(struct pm8001_hba_info *pm8001_ha,
		struct pm8001_device *pm8001_ha_dev)
{
	int res;
	u32 ccb_tag;
	struct pm8001_ccb_info *ccb;
	struct sas_task *task = NULL;
	struct task_abort_req task_abort;
	struct inbound_queue_table *circularQ;
	u32 opc = OPC_INB_SATA_ABORT;
	int ret;

	if (!pm8001_ha_dev) {
		pm8001_dbg(pm8001_ha, FAIL, "dev is null\n");
		return;
	}

	task = sas_alloc_slow_task(GFP_ATOMIC);

	if (!task) {
		pm8001_dbg(pm8001_ha, FAIL, "cannot allocate task\n");
		return;
	}

	task->task_done = pm8001_task_done;

	res = pm8001_tag_alloc(pm8001_ha, &ccb_tag);
	if (res) {
		sas_free_task(task);
		return;
	}

	ccb = &pm8001_ha->ccb_info[ccb_tag];
	ccb->device = pm8001_ha_dev;
	ccb->ccb_tag = ccb_tag;
	ccb->task = task;

	circularQ = &pm8001_ha->inbnd_q_tbl[0];

	memset(&task_abort, 0, sizeof(task_abort));
	task_abort.abort_all = cpu_to_le32(1);
	task_abort.device_id = cpu_to_le32(pm8001_ha_dev->device_id);
	task_abort.tag = cpu_to_le32(ccb_tag);

	ret = pm8001_mpi_build_cmd(pm8001_ha, circularQ, opc, &task_abort,
			sizeof(task_abort), 0);
	pm8001_dbg(pm8001_ha, FAIL, "Executing abort task end\n");
	if (ret) {
		sas_free_task(task);
		pm8001_tag_free(pm8001_ha, ccb_tag);
	}
}

static void pm80xx_send_read_log(struct pm8001_hba_info *pm8001_ha,
		struct pm8001_device *pm8001_ha_dev)
{
	struct sata_start_req sata_cmd;
	int res;
	u32 ccb_tag;
	struct pm8001_ccb_info *ccb;
	struct sas_task *task = NULL;
	struct host_to_dev_fis fis;
	struct domain_device *dev;
	struct inbound_queue_table *circularQ;
	u32 opc = OPC_INB_SATA_HOST_OPSTART;

	task = sas_alloc_slow_task(GFP_ATOMIC);

	if (!task) {
		pm8001_dbg(pm8001_ha, FAIL, "cannot allocate task !!!\n");
		return;
	}
	task->task_done = pm8001_task_done;

	res = pm8001_tag_alloc(pm8001_ha, &ccb_tag);
	if (res) {
		sas_free_task(task);
		pm8001_dbg(pm8001_ha, FAIL, "cannot allocate tag !!!\n");
		return;
	}

	/* allocate domain device by ourselves as libsas
	 * is not going to provide any
	*/
	dev = kzalloc(sizeof(struct domain_device), GFP_ATOMIC);
	if (!dev) {
		sas_free_task(task);
		pm8001_tag_free(pm8001_ha, ccb_tag);
		pm8001_dbg(pm8001_ha, FAIL,
			   "Domain device cannot be allocated\n");
		return;
	}

	task->dev = dev;
	task->dev->lldd_dev = pm8001_ha_dev;

	ccb = &pm8001_ha->ccb_info[ccb_tag];
	ccb->device = pm8001_ha_dev;
	ccb->ccb_tag = ccb_tag;
	ccb->task = task;
	ccb->n_elem = 0;
	pm8001_ha_dev->id |= NCQ_READ_LOG_FLAG;
	pm8001_ha_dev->id |= NCQ_2ND_RLE_FLAG;

	memset(&sata_cmd, 0, sizeof(sata_cmd));
	circularQ = &pm8001_ha->inbnd_q_tbl[0];

	/* construct read log FIS */
	memset(&fis, 0, sizeof(struct host_to_dev_fis));
	fis.fis_type = 0x27;
	fis.flags = 0x80;
	fis.command = ATA_CMD_READ_LOG_EXT;
	fis.lbal = 0x10;
	fis.sector_count = 0x1;

	sata_cmd.tag = cpu_to_le32(ccb_tag);
	sata_cmd.device_id = cpu_to_le32(pm8001_ha_dev->device_id);
	sata_cmd.ncqtag_atap_dir_m_dad |= ((0x1 << 7) | (0x5 << 9));
	memcpy(&sata_cmd.sata_fis, &fis, sizeof(struct host_to_dev_fis));

	res = pm8001_mpi_build_cmd(pm8001_ha, circularQ, opc, &sata_cmd,
			sizeof(sata_cmd), 0);
	pm8001_dbg(pm8001_ha, FAIL, "Executing read log end\n");
	if (res) {
		sas_free_task(task);
		pm8001_tag_free(pm8001_ha, ccb_tag);
		kfree(dev);
	}
}

/**
 * mpi_ssp_completion - process the event that FW response to the SSP request.
 * @pm8001_ha: our hba card information
 * @piomb: the message contents of this outbound message.
 *
 * When FW has completed a ssp request for example a IO request, after it has
 * filled the SG data with the data, it will trigger this event representing
 * that he has finished the job; please check the corresponding buffer.
 * So we will tell the caller who maybe waiting the result to tell upper layer
 * that the task has been finished.
 */
static void
mpi_ssp_completion(struct pm8001_hba_info *pm8001_ha, void *piomb)
{
	struct sas_task *t;
	struct pm8001_ccb_info *ccb;
	unsigned long flags;
	u32 status;
	u32 param;
	u32 tag;
	struct ssp_completion_resp *psspPayload;
	struct task_status_struct *ts;
	struct ssp_response_iu *iu;
	struct pm8001_device *pm8001_dev;
	psspPayload = (struct ssp_completion_resp *)(piomb + 4);
	status = le32_to_cpu(psspPayload->status);
	tag = le32_to_cpu(psspPayload->tag);
	ccb = &pm8001_ha->ccb_info[tag];
	if ((status == IO_ABORTED) && ccb->open_retry) {
		/* Being completed by another */
		ccb->open_retry = 0;
		return;
	}
	pm8001_dev = ccb->device;
	param = le32_to_cpu(psspPayload->param);
	t = ccb->task;

	if (status && status != IO_UNDERFLOW)
		pm8001_dbg(pm8001_ha, FAIL, "sas IO status 0x%x\n", status);
	if (unlikely(!t || !t->lldd_task || !t->dev))
		return;
	ts = &t->task_status;

	pm8001_dbg(pm8001_ha, DEV,
		   "tag::0x%x, status::0x%x task::0x%p\n", tag, status, t);

	/* Print sas address of IO failed device */
	if ((status != IO_SUCCESS) && (status != IO_OVERFLOW) &&
		(status != IO_UNDERFLOW))
		pm8001_dbg(pm8001_ha, FAIL, "SAS Address of IO Failure Drive:%016llx\n",
			   SAS_ADDR(t->dev->sas_addr));

	switch (status) {
	case IO_SUCCESS:
		pm8001_dbg(pm8001_ha, IO, "IO_SUCCESS ,param = 0x%x\n",
			   param);
		if (param == 0) {
			ts->resp = SAS_TASK_COMPLETE;
			ts->stat = SAS_SAM_STAT_GOOD;
		} else {
			ts->resp = SAS_TASK_COMPLETE;
			ts->stat = SAS_PROTO_RESPONSE;
			ts->residual = param;
			iu = &psspPayload->ssp_resp_iu;
			sas_ssp_task_response(pm8001_ha->dev, t, iu);
		}
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_ABORTED:
		pm8001_dbg(pm8001_ha, IO, "IO_ABORTED IOMB Tag\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_ABORTED_TASK;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_UNDERFLOW:
		/* SSP Completion with error */
		pm8001_dbg(pm8001_ha, IO, "IO_UNDERFLOW ,param = 0x%x\n",
			   param);
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_UNDERRUN;
		ts->residual = param;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_NO_DEVICE:
		pm8001_dbg(pm8001_ha, IO, "IO_NO_DEVICE\n");
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_PHY_DOWN;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_BREAK:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_BREAK\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		/* Force the midlayer to retry */
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_PHY_NOT_READY:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_PHY_NOT_READY\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_INVALID_SSP_RSP_FRAME:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_XFER_ERROR_INVALID_SSP_RSP_FRAME\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_EPROTO;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_OPEN_CNX_ERROR_ZONE_VIOLATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_ZONE_VIOLATION\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_UNKNOWN;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_OPEN_CNX_ERROR_BREAK:
		pm8001_dbg(pm8001_ha, IO, "IO_OPEN_CNX_ERROR_BREAK\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS:
	case IO_XFER_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED:
		pm8001_dbg(pm8001_ha, IO, "IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_UNKNOWN;
		if (!t->uldd_task)
			pm8001_handle_event(pm8001_ha,
				pm8001_dev,
				IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS);
		break;
	case IO_OPEN_CNX_ERROR_BAD_DESTINATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_BAD_DESTINATION\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_BAD_DEST;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_CONN_RATE;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_OPEN_CNX_ERROR_WRONG_DESTINATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_WRONG_DESTINATION\n");
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_WRONG_DEST;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_NAK_RECEIVED:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_NAK_RECEIVED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_ACK_NAK_TIMEOUT:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_ACK_NAK_TIMEOUT\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_NAK_R_ERR;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_DMA:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_DMA\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_OPEN_RETRY_TIMEOUT:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_OPEN_RETRY_TIMEOUT\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_OFFSET_MISMATCH:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_OFFSET_MISMATCH\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_PORT_IN_RESET:
		pm8001_dbg(pm8001_ha, IO, "IO_PORT_IN_RESET\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_DS_NON_OPERATIONAL:
		pm8001_dbg(pm8001_ha, IO, "IO_DS_NON_OPERATIONAL\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		if (!t->uldd_task)
			pm8001_handle_event(pm8001_ha,
				pm8001_dev,
				IO_DS_NON_OPERATIONAL);
		break;
	case IO_DS_IN_RECOVERY:
		pm8001_dbg(pm8001_ha, IO, "IO_DS_IN_RECOVERY\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_TM_TAG_NOT_FOUND:
		pm8001_dbg(pm8001_ha, IO, "IO_TM_TAG_NOT_FOUND\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_SSP_EXT_IU_ZERO_LEN_ERROR:
		pm8001_dbg(pm8001_ha, IO, "IO_SSP_EXT_IU_ZERO_LEN_ERROR\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	default:
		pm8001_dbg(pm8001_ha, DEVIO, "Unknown status 0x%x\n", status);
		/* not allowed case. Therefore, return failed status */
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	}
	pm8001_dbg(pm8001_ha, IO, "scsi_status = 0x%x\n ",
		   psspPayload->ssp_resp_iu.status);
	spin_lock_irqsave(&t->task_state_lock, flags);
	t->task_state_flags &= ~SAS_TASK_STATE_PENDING;
	t->task_state_flags &= ~SAS_TASK_AT_INITIATOR;
	t->task_state_flags |= SAS_TASK_STATE_DONE;
	if (unlikely((t->task_state_flags & SAS_TASK_STATE_ABORTED))) {
		spin_unlock_irqrestore(&t->task_state_lock, flags);
		pm8001_dbg(pm8001_ha, FAIL,
			   "task 0x%p done with io_status 0x%x resp 0x%x stat 0x%x but aborted by upper layer!\n",
			   t, status, ts->resp, ts->stat);
		if (t->slow_task)
			complete(&t->slow_task->completion);
		pm8001_ccb_task_free(pm8001_ha, t, ccb, tag);
	} else {
		spin_unlock_irqrestore(&t->task_state_lock, flags);
		pm8001_ccb_task_free(pm8001_ha, t, ccb, tag);
		mb();/* in order to force CPU ordering */
		t->task_done(t);
	}
}

/*See the comments for mpi_ssp_completion */
static void mpi_ssp_event(struct pm8001_hba_info *pm8001_ha, void *piomb)
{
	struct sas_task *t;
	unsigned long flags;
	struct task_status_struct *ts;
	struct pm8001_ccb_info *ccb;
	struct pm8001_device *pm8001_dev;
	struct ssp_event_resp *psspPayload =
		(struct ssp_event_resp *)(piomb + 4);
	u32 event = le32_to_cpu(psspPayload->event);
	u32 tag = le32_to_cpu(psspPayload->tag);
	u32 port_id = le32_to_cpu(psspPayload->port_id);

	ccb = &pm8001_ha->ccb_info[tag];
	t = ccb->task;
	pm8001_dev = ccb->device;
	if (event)
		pm8001_dbg(pm8001_ha, FAIL, "sas IO status 0x%x\n", event);
	if (unlikely(!t || !t->lldd_task || !t->dev))
		return;
	ts = &t->task_status;
	pm8001_dbg(pm8001_ha, IOERR, "port_id:0x%x, tag:0x%x, event:0x%x\n",
		   port_id, tag, event);
	switch (event) {
	case IO_OVERFLOW:
		pm8001_dbg(pm8001_ha, IO, "IO_UNDERFLOW\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_OVERRUN;
		ts->residual = 0;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_BREAK:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_BREAK\n");
		pm8001_handle_event(pm8001_ha, t, IO_XFER_ERROR_BREAK);
		return;
	case IO_XFER_ERROR_PHY_NOT_READY:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_PHY_NOT_READY\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		break;
	case IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_EPROTO;
		break;
	case IO_OPEN_CNX_ERROR_ZONE_VIOLATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_ZONE_VIOLATION\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_UNKNOWN;
		break;
	case IO_OPEN_CNX_ERROR_BREAK:
		pm8001_dbg(pm8001_ha, IO, "IO_OPEN_CNX_ERROR_BREAK\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		break;
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS:
	case IO_XFER_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED:
		pm8001_dbg(pm8001_ha, IO, "IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_UNKNOWN;
		if (!t->uldd_task)
			pm8001_handle_event(pm8001_ha,
				pm8001_dev,
				IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS);
		break;
	case IO_OPEN_CNX_ERROR_BAD_DESTINATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_BAD_DESTINATION\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_BAD_DEST;
		break;
	case IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_CONN_RATE;
		break;
	case IO_OPEN_CNX_ERROR_WRONG_DESTINATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_WRONG_DESTINATION\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_WRONG_DEST;
		break;
	case IO_XFER_ERROR_NAK_RECEIVED:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_NAK_RECEIVED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		break;
	case IO_XFER_ERROR_ACK_NAK_TIMEOUT:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_ACK_NAK_TIMEOUT\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_NAK_R_ERR;
		break;
	case IO_XFER_OPEN_RETRY_TIMEOUT:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_OPEN_RETRY_TIMEOUT\n");
		pm8001_handle_event(pm8001_ha, t, IO_XFER_OPEN_RETRY_TIMEOUT);
		return;
	case IO_XFER_ERROR_UNEXPECTED_PHASE:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_UNEXPECTED_PHASE\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_OVERRUN;
		break;
	case IO_XFER_ERROR_XFER_RDY_OVERRUN:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_XFER_RDY_OVERRUN\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_OVERRUN;
		break;
	case IO_XFER_ERROR_XFER_RDY_NOT_EXPECTED:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_XFER_ERROR_XFER_RDY_NOT_EXPECTED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_OVERRUN;
		break;
	case IO_XFER_ERROR_CMD_ISSUE_ACK_NAK_TIMEOUT:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_XFER_ERROR_CMD_ISSUE_ACK_NAK_TIMEOUT\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_OVERRUN;
		break;
	case IO_XFER_ERROR_OFFSET_MISMATCH:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_OFFSET_MISMATCH\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_OVERRUN;
		break;
	case IO_XFER_ERROR_XFER_ZERO_DATA_LEN:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_XFER_ERROR_XFER_ZERO_DATA_LEN\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_OVERRUN;
		break;
	case IO_XFER_ERROR_INTERNAL_CRC_ERROR:
		pm8001_dbg(pm8001_ha, IOERR,
			   "IO_XFR_ERROR_INTERNAL_CRC_ERROR\n");
		/* TBC: used default set values */
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_OVERRUN;
		break;
	case IO_XFER_CMD_FRAME_ISSUED:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_CMD_FRAME_ISSUED\n");
		return;
	default:
		pm8001_dbg(pm8001_ha, DEVIO, "Unknown status 0x%x\n", event);
		/* not allowed case. Therefore, return failed status */
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_OVERRUN;
		break;
	}
	spin_lock_irqsave(&t->task_state_lock, flags);
	t->task_state_flags &= ~SAS_TASK_STATE_PENDING;
	t->task_state_flags &= ~SAS_TASK_AT_INITIATOR;
	t->task_state_flags |= SAS_TASK_STATE_DONE;
	if (unlikely((t->task_state_flags & SAS_TASK_STATE_ABORTED))) {
		spin_unlock_irqrestore(&t->task_state_lock, flags);
		pm8001_dbg(pm8001_ha, FAIL,
			   "task 0x%p done with event 0x%x resp 0x%x stat 0x%x but aborted by upper layer!\n",
			   t, event, ts->resp, ts->stat);
		pm8001_ccb_task_free(pm8001_ha, t, ccb, tag);
	} else {
		spin_unlock_irqrestore(&t->task_state_lock, flags);
		pm8001_ccb_task_free(pm8001_ha, t, ccb, tag);
		mb();/* in order to force CPU ordering */
		t->task_done(t);
	}
}

/*See the comments for mpi_ssp_completion */
static void
mpi_sata_completion(struct pm8001_hba_info *pm8001_ha,
		struct outbound_queue_table *circularQ, void *piomb)
{
	struct sas_task *t;
	struct pm8001_ccb_info *ccb;
	u32 param;
	u32 status;
	u32 tag;
	int i, j;
	u8 sata_addr_low[4];
	u32 temp_sata_addr_low, temp_sata_addr_hi;
	u8 sata_addr_hi[4];
	struct sata_completion_resp *psataPayload;
	struct task_status_struct *ts;
	struct ata_task_resp *resp ;
	u32 *sata_resp;
	struct pm8001_device *pm8001_dev;
	unsigned long flags;

	psataPayload = (struct sata_completion_resp *)(piomb + 4);
	status = le32_to_cpu(psataPayload->status);
	param = le32_to_cpu(psataPayload->param);
	tag = le32_to_cpu(psataPayload->tag);

	if (!tag) {
		pm8001_dbg(pm8001_ha, FAIL, "tag null\n");
		return;
	}

	ccb = &pm8001_ha->ccb_info[tag];
	t = ccb->task;
	pm8001_dev = ccb->device;

	if (t) {
		if (t->dev && (t->dev->lldd_dev))
			pm8001_dev = t->dev->lldd_dev;
	} else {
		pm8001_dbg(pm8001_ha, FAIL, "task null\n");
		return;
	}

	if ((pm8001_dev && !(pm8001_dev->id & NCQ_READ_LOG_FLAG))
		&& unlikely(!t || !t->lldd_task || !t->dev)) {
		pm8001_dbg(pm8001_ha, FAIL, "task or dev null\n");
		return;
	}

	ts = &t->task_status;

	if (status != IO_SUCCESS) {
		pm8001_dbg(pm8001_ha, FAIL,
			"IO failed device_id %u status 0x%x tag %d\n",
			pm8001_dev->device_id, status, tag);
	}

	/* Print sas address of IO failed device */
	if ((status != IO_SUCCESS) && (status != IO_OVERFLOW) &&
		(status != IO_UNDERFLOW)) {
		if (!((t->dev->parent) &&
			(dev_is_expander(t->dev->parent->dev_type)))) {
			for (i = 0, j = 4; i <= 3 && j <= 7; i++, j++)
				sata_addr_low[i] = pm8001_ha->sas_addr[j];
			for (i = 0, j = 0; i <= 3 && j <= 3; i++, j++)
				sata_addr_hi[i] = pm8001_ha->sas_addr[j];
			memcpy(&temp_sata_addr_low, sata_addr_low,
				sizeof(sata_addr_low));
			memcpy(&temp_sata_addr_hi, sata_addr_hi,
				sizeof(sata_addr_hi));
			temp_sata_addr_hi = (((temp_sata_addr_hi >> 24) & 0xff)
						|((temp_sata_addr_hi << 8) &
						0xff0000) |
						((temp_sata_addr_hi >> 8)
						& 0xff00) |
						((temp_sata_addr_hi << 24) &
						0xff000000));
			temp_sata_addr_low = ((((temp_sata_addr_low >> 24)
						& 0xff) |
						((temp_sata_addr_low << 8)
						& 0xff0000) |
						((temp_sata_addr_low >> 8)
						& 0xff00) |
						((temp_sata_addr_low << 24)
						& 0xff000000)) +
						pm8001_dev->attached_phy +
						0x10);
			pm8001_dbg(pm8001_ha, FAIL,
				   "SAS Address of IO Failure Drive:%08x%08x\n",
				   temp_sata_addr_hi,
				   temp_sata_addr_low);

		} else {
			pm8001_dbg(pm8001_ha, FAIL,
				   "SAS Address of IO Failure Drive:%016llx\n",
				   SAS_ADDR(t->dev->sas_addr));
		}
	}
	switch (status) {
	case IO_SUCCESS:
		pm8001_dbg(pm8001_ha, IO, "IO_SUCCESS\n");
		if (param == 0) {
			ts->resp = SAS_TASK_COMPLETE;
			ts->stat = SAS_SAM_STAT_GOOD;
			/* check if response is for SEND READ LOG */
			if (pm8001_dev &&
				(pm8001_dev->id & NCQ_READ_LOG_FLAG)) {
				/* set new bit for abort_all */
				pm8001_dev->id |= NCQ_ABORT_ALL_FLAG;
				/* clear bit for read log */
				pm8001_dev->id = pm8001_dev->id & 0x7FFFFFFF;
				pm80xx_send_abort_all(pm8001_ha, pm8001_dev);
				/* Free the tag */
				pm8001_tag_free(pm8001_ha, tag);
				sas_free_task(t);
				return;
			}
		} else {
			u8 len;
			ts->resp = SAS_TASK_COMPLETE;
			ts->stat = SAS_PROTO_RESPONSE;
			ts->residual = param;
			pm8001_dbg(pm8001_ha, IO,
				   "SAS_PROTO_RESPONSE len = %d\n",
				   param);
			sata_resp = &psataPayload->sata_resp[0];
			resp = (struct ata_task_resp *)ts->buf;
			if (t->ata_task.dma_xfer == 0 &&
			    t->data_dir == DMA_FROM_DEVICE) {
				len = sizeof(struct pio_setup_fis);
				pm8001_dbg(pm8001_ha, IO,
					   "PIO read len = %d\n", len);
			} else if (t->ata_task.use_ncq) {
				len = sizeof(struct set_dev_bits_fis);
				pm8001_dbg(pm8001_ha, IO, "FPDMA len = %d\n",
					   len);
			} else {
				len = sizeof(struct dev_to_host_fis);
				pm8001_dbg(pm8001_ha, IO, "other len = %d\n",
					   len);
			}
			if (SAS_STATUS_BUF_SIZE >= sizeof(*resp)) {
				resp->frame_len = len;
				memcpy(&resp->ending_fis[0], sata_resp, len);
				ts->buf_valid_size = sizeof(*resp);
			} else
				pm8001_dbg(pm8001_ha, IO,
					   "response too large\n");
		}
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_ABORTED:
		pm8001_dbg(pm8001_ha, IO, "IO_ABORTED IOMB Tag\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_ABORTED_TASK;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
		/* following cases are to do cases */
	case IO_UNDERFLOW:
		/* SATA Completion with error */
		pm8001_dbg(pm8001_ha, IO, "IO_UNDERFLOW param = %d\n", param);
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_UNDERRUN;
		ts->residual = param;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_NO_DEVICE:
		pm8001_dbg(pm8001_ha, IO, "IO_NO_DEVICE\n");
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_PHY_DOWN;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_BREAK:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_BREAK\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_INTERRUPTED;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_PHY_NOT_READY:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_PHY_NOT_READY\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_EPROTO;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_OPEN_CNX_ERROR_ZONE_VIOLATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_ZONE_VIOLATION\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_UNKNOWN;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_OPEN_CNX_ERROR_BREAK:
		pm8001_dbg(pm8001_ha, IO, "IO_OPEN_CNX_ERROR_BREAK\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_CONT0;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS:
	case IO_XFER_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED:
		pm8001_dbg(pm8001_ha, IO, "IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DEV_NO_RESPONSE;
		if (!t->uldd_task) {
			pm8001_handle_event(pm8001_ha,
				pm8001_dev,
				IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS);
			ts->resp = SAS_TASK_UNDELIVERED;
			ts->stat = SAS_QUEUE_FULL;
			spin_unlock_irqrestore(&circularQ->oq_lock,
					circularQ->lock_flags);
			pm8001_ccb_task_free_done(pm8001_ha, t, ccb, tag);
			spin_lock_irqsave(&circularQ->oq_lock,
					circularQ->lock_flags);
			return;
		}
		break;
	case IO_OPEN_CNX_ERROR_BAD_DESTINATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_BAD_DESTINATION\n");
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_BAD_DEST;
		if (!t->uldd_task) {
			pm8001_handle_event(pm8001_ha,
				pm8001_dev,
				IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS);
			ts->resp = SAS_TASK_UNDELIVERED;
			ts->stat = SAS_QUEUE_FULL;
			spin_unlock_irqrestore(&circularQ->oq_lock,
					circularQ->lock_flags);
			pm8001_ccb_task_free_done(pm8001_ha, t, ccb, tag);
			spin_lock_irqsave(&circularQ->oq_lock,
					circularQ->lock_flags);
			return;
		}
		break;
	case IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_CONN_RATE;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DEV_NO_RESPONSE;
		if (!t->uldd_task) {
			pm8001_handle_event(pm8001_ha,
				pm8001_dev,
				IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY);
			ts->resp = SAS_TASK_UNDELIVERED;
			ts->stat = SAS_QUEUE_FULL;
			spin_unlock_irqrestore(&circularQ->oq_lock,
					circularQ->lock_flags);
			pm8001_ccb_task_free_done(pm8001_ha, t, ccb, tag);
			spin_lock_irqsave(&circularQ->oq_lock,
					circularQ->lock_flags);
			return;
		}
		break;
	case IO_OPEN_CNX_ERROR_WRONG_DESTINATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_WRONG_DESTINATION\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_WRONG_DEST;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_NAK_RECEIVED:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_NAK_RECEIVED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_NAK_R_ERR;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_ACK_NAK_TIMEOUT:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_ACK_NAK_TIMEOUT\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_NAK_R_ERR;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_DMA:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_DMA\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_ABORTED_TASK;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_SATA_LINK_TIMEOUT:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_SATA_LINK_TIMEOUT\n");
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_DEV_NO_RESPONSE;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_ERROR_REJECTED_NCQ_MODE:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_REJECTED_NCQ_MODE\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_UNDERRUN;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_XFER_OPEN_RETRY_TIMEOUT:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_OPEN_RETRY_TIMEOUT\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_TO;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_PORT_IN_RESET:
		pm8001_dbg(pm8001_ha, IO, "IO_PORT_IN_RESET\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DEV_NO_RESPONSE;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_DS_NON_OPERATIONAL:
		pm8001_dbg(pm8001_ha, IO, "IO_DS_NON_OPERATIONAL\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DEV_NO_RESPONSE;
		if (!t->uldd_task) {
			pm8001_handle_event(pm8001_ha, pm8001_dev,
					IO_DS_NON_OPERATIONAL);
			ts->resp = SAS_TASK_UNDELIVERED;
			ts->stat = SAS_QUEUE_FULL;
			spin_unlock_irqrestore(&circularQ->oq_lock,
					circularQ->lock_flags);
			pm8001_ccb_task_free_done(pm8001_ha, t, ccb, tag);
			spin_lock_irqsave(&circularQ->oq_lock,
					circularQ->lock_flags);
			return;
		}
		break;
	case IO_DS_IN_RECOVERY:
		pm8001_dbg(pm8001_ha, IO, "IO_DS_IN_RECOVERY\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DEV_NO_RESPONSE;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_DS_IN_ERROR:
		pm8001_dbg(pm8001_ha, IO, "IO_DS_IN_ERROR\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DEV_NO_RESPONSE;
		if (!t->uldd_task) {
			pm8001_handle_event(pm8001_ha, pm8001_dev,
					IO_DS_IN_ERROR);
			ts->resp = SAS_TASK_UNDELIVERED;
			ts->stat = SAS_QUEUE_FULL;
			spin_unlock_irqrestore(&circularQ->oq_lock,
					circularQ->lock_flags);
			pm8001_ccb_task_free_done(pm8001_ha, t, ccb, tag);
			spin_lock_irqsave(&circularQ->oq_lock,
					circularQ->lock_flags);
			return;
		}
		break;
	case IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	default:
		pm8001_dbg(pm8001_ha, DEVIO,
				"Unknown status device_id %u status 0x%x tag %d\n",
			pm8001_dev->device_id, status, tag);
		/* not allowed case. Therefore, return failed status */
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DEV_NO_RESPONSE;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	}
	spin_lock_irqsave(&t->task_state_lock, flags);
	t->task_state_flags &= ~SAS_TASK_STATE_PENDING;
	t->task_state_flags &= ~SAS_TASK_AT_INITIATOR;
	t->task_state_flags |= SAS_TASK_STATE_DONE;
	if (unlikely((t->task_state_flags & SAS_TASK_STATE_ABORTED))) {
		spin_unlock_irqrestore(&t->task_state_lock, flags);
		pm8001_dbg(pm8001_ha, FAIL,
			   "task 0x%p done with io_status 0x%x resp 0x%x stat 0x%x but aborted by upper layer!\n",
			   t, status, ts->resp, ts->stat);
		if (t->slow_task)
			complete(&t->slow_task->completion);
		pm8001_ccb_task_free(pm8001_ha, t, ccb, tag);
	} else {
		spin_unlock_irqrestore(&t->task_state_lock, flags);
		spin_unlock_irqrestore(&circularQ->oq_lock,
				circularQ->lock_flags);
		pm8001_ccb_task_free_done(pm8001_ha, t, ccb, tag);
		spin_lock_irqsave(&circularQ->oq_lock,
				circularQ->lock_flags);
	}
}

/*See the comments for mpi_ssp_completion */
static void mpi_sata_event(struct pm8001_hba_info *pm8001_ha,
		struct outbound_queue_table *circularQ, void *piomb)
{
	struct sas_task *t;
	struct task_status_struct *ts;
	struct pm8001_ccb_info *ccb;
	struct pm8001_device *pm8001_dev;
	struct sata_event_resp *psataPayload =
		(struct sata_event_resp *)(piomb + 4);
	u32 event = le32_to_cpu(psataPayload->event);
	u32 tag = le32_to_cpu(psataPayload->tag);
	u32 port_id = le32_to_cpu(psataPayload->port_id);
	u32 dev_id = le32_to_cpu(psataPayload->device_id);

	if (event)
		pm8001_dbg(pm8001_ha, FAIL, "SATA EVENT 0x%x\n", event);

	/* Check if this is NCQ error */
	if (event == IO_XFER_ERROR_ABORTED_NCQ_MODE) {
		/* find device using device id */
		pm8001_dev = pm8001_find_dev(pm8001_ha, dev_id);
		/* send read log extension */
		if (pm8001_dev)
			pm80xx_send_read_log(pm8001_ha, pm8001_dev);
		return;
	}

	ccb = &pm8001_ha->ccb_info[tag];
	t = ccb->task;
	pm8001_dev = ccb->device;

	if (unlikely(!t || !t->lldd_task || !t->dev)) {
		pm8001_dbg(pm8001_ha, FAIL, "task or dev null\n");
		return;
	}

	ts = &t->task_status;
	pm8001_dbg(pm8001_ha, IOERR, "port_id:0x%x, tag:0x%x, event:0x%x\n",
		   port_id, tag, event);
	switch (event) {
	case IO_OVERFLOW:
		pm8001_dbg(pm8001_ha, IO, "IO_UNDERFLOW\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_OVERRUN;
		ts->residual = 0;
		break;
	case IO_XFER_ERROR_BREAK:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_BREAK\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_INTERRUPTED;
		break;
	case IO_XFER_ERROR_PHY_NOT_READY:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_PHY_NOT_READY\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		break;
	case IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_EPROTO;
		break;
	case IO_OPEN_CNX_ERROR_ZONE_VIOLATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_ZONE_VIOLATION\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_UNKNOWN;
		break;
	case IO_OPEN_CNX_ERROR_BREAK:
		pm8001_dbg(pm8001_ha, IO, "IO_OPEN_CNX_ERROR_BREAK\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_CONT0;
		break;
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS:
	case IO_XFER_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED:
		pm8001_dbg(pm8001_ha, FAIL,
			   "IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS\n");
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_DEV_NO_RESPONSE;
		if (!t->uldd_task) {
			pm8001_handle_event(pm8001_ha,
				pm8001_dev,
				IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS);
			ts->resp = SAS_TASK_COMPLETE;
			ts->stat = SAS_QUEUE_FULL;
			return;
		}
		break;
	case IO_OPEN_CNX_ERROR_BAD_DESTINATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_BAD_DESTINATION\n");
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_BAD_DEST;
		break;
	case IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_CONN_RATE;
		break;
	case IO_OPEN_CNX_ERROR_WRONG_DESTINATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_WRONG_DESTINATION\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_WRONG_DEST;
		break;
	case IO_XFER_ERROR_NAK_RECEIVED:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_NAK_RECEIVED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_NAK_R_ERR;
		break;
	case IO_XFER_ERROR_PEER_ABORTED:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_PEER_ABORTED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_NAK_R_ERR;
		break;
	case IO_XFER_ERROR_REJECTED_NCQ_MODE:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_REJECTED_NCQ_MODE\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_UNDERRUN;
		break;
	case IO_XFER_OPEN_RETRY_TIMEOUT:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_OPEN_RETRY_TIMEOUT\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_TO;
		break;
	case IO_XFER_ERROR_UNEXPECTED_PHASE:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_UNEXPECTED_PHASE\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_TO;
		break;
	case IO_XFER_ERROR_XFER_RDY_OVERRUN:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_XFER_RDY_OVERRUN\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_TO;
		break;
	case IO_XFER_ERROR_XFER_RDY_NOT_EXPECTED:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_XFER_ERROR_XFER_RDY_NOT_EXPECTED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_TO;
		break;
	case IO_XFER_ERROR_OFFSET_MISMATCH:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_OFFSET_MISMATCH\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_TO;
		break;
	case IO_XFER_ERROR_XFER_ZERO_DATA_LEN:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_XFER_ERROR_XFER_ZERO_DATA_LEN\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_TO;
		break;
	case IO_XFER_CMD_FRAME_ISSUED:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_CMD_FRAME_ISSUED\n");
		break;
	case IO_XFER_PIO_SETUP_ERROR:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_PIO_SETUP_ERROR\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_TO;
		break;
	case IO_XFER_ERROR_INTERNAL_CRC_ERROR:
		pm8001_dbg(pm8001_ha, FAIL,
			   "IO_XFR_ERROR_INTERNAL_CRC_ERROR\n");
		/* TBC: used default set values */
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_TO;
		break;
	case IO_XFER_DMA_ACTIVATE_TIMEOUT:
		pm8001_dbg(pm8001_ha, FAIL, "IO_XFR_DMA_ACTIVATE_TIMEOUT\n");
		/* TBC: used default set values */
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_TO;
		break;
	default:
		pm8001_dbg(pm8001_ha, IO, "Unknown status 0x%x\n", event);
		/* not allowed case. Therefore, return failed status */
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_TO;
		break;
	}
}

/*See the comments for mpi_ssp_completion */
static void
mpi_smp_completion(struct pm8001_hba_info *pm8001_ha, void *piomb)
{
	u32 param, i;
	struct sas_task *t;
	struct pm8001_ccb_info *ccb;
	unsigned long flags;
	u32 status;
	u32 tag;
	struct smp_completion_resp *psmpPayload;
	struct task_status_struct *ts;
	struct pm8001_device *pm8001_dev;

	psmpPayload = (struct smp_completion_resp *)(piomb + 4);
	status = le32_to_cpu(psmpPayload->status);
	tag = le32_to_cpu(psmpPayload->tag);

	ccb = &pm8001_ha->ccb_info[tag];
	param = le32_to_cpu(psmpPayload->param);
	t = ccb->task;
	ts = &t->task_status;
	pm8001_dev = ccb->device;
	if (status)
		pm8001_dbg(pm8001_ha, FAIL, "smp IO status 0x%x\n", status);
	if (unlikely(!t || !t->lldd_task || !t->dev))
		return;

	pm8001_dbg(pm8001_ha, DEV, "tag::0x%x status::0x%x\n", tag, status);

	switch (status) {

	case IO_SUCCESS:
		pm8001_dbg(pm8001_ha, IO, "IO_SUCCESS\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_SAM_STAT_GOOD;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		if (pm8001_ha->smp_exp_mode == SMP_DIRECT) {
			struct scatterlist *sg_resp = &t->smp_task.smp_resp;
			u8 *payload;
			void *to;

			pm8001_dbg(pm8001_ha, IO,
				   "DIRECT RESPONSE Length:%d\n",
				   param);
			to = kmap_atomic(sg_page(sg_resp));
			payload = to + sg_resp->offset;
			for (i = 0; i < param; i++) {
				*(payload + i) = psmpPayload->_r_a[i];
				pm8001_dbg(pm8001_ha, IO,
					   "SMP Byte%d DMA data 0x%x psmp 0x%x\n",
					   i, *(payload + i),
					   psmpPayload->_r_a[i]);
			}
			kunmap_atomic(to);
		}
		break;
	case IO_ABORTED:
		pm8001_dbg(pm8001_ha, IO, "IO_ABORTED IOMB\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_ABORTED_TASK;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_OVERFLOW:
		pm8001_dbg(pm8001_ha, IO, "IO_UNDERFLOW\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_OVERRUN;
		ts->residual = 0;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		break;
	case IO_NO_DEVICE:
		pm8001_dbg(pm8001_ha, IO, "IO_NO_DEVICE\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_PHY_DOWN;
		break;
	case IO_ERROR_HW_TIMEOUT:
		pm8001_dbg(pm8001_ha, IO, "IO_ERROR_HW_TIMEOUT\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_SAM_STAT_BUSY;
		break;
	case IO_XFER_ERROR_BREAK:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_BREAK\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_SAM_STAT_BUSY;
		break;
	case IO_XFER_ERROR_PHY_NOT_READY:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_PHY_NOT_READY\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_SAM_STAT_BUSY;
		break;
	case IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_UNKNOWN;
		break;
	case IO_OPEN_CNX_ERROR_ZONE_VIOLATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_ZONE_VIOLATION\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_UNKNOWN;
		break;
	case IO_OPEN_CNX_ERROR_BREAK:
		pm8001_dbg(pm8001_ha, IO, "IO_OPEN_CNX_ERROR_BREAK\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_CONT0;
		break;
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS:
	case IO_XFER_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE:
	case IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED:
		pm8001_dbg(pm8001_ha, IO, "IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_UNKNOWN;
		pm8001_handle_event(pm8001_ha,
				pm8001_dev,
				IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS);
		break;
	case IO_OPEN_CNX_ERROR_BAD_DESTINATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_BAD_DESTINATION\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_BAD_DEST;
		break;
	case IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_CONN_RATE;
		break;
	case IO_OPEN_CNX_ERROR_WRONG_DESTINATION:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_WRONG_DESTINATION\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_WRONG_DEST;
		break;
	case IO_XFER_ERROR_RX_FRAME:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_ERROR_RX_FRAME\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DEV_NO_RESPONSE;
		break;
	case IO_XFER_OPEN_RETRY_TIMEOUT:
		pm8001_dbg(pm8001_ha, IO, "IO_XFER_OPEN_RETRY_TIMEOUT\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		break;
	case IO_ERROR_INTERNAL_SMP_RESOURCE:
		pm8001_dbg(pm8001_ha, IO, "IO_ERROR_INTERNAL_SMP_RESOURCE\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_QUEUE_FULL;
		break;
	case IO_PORT_IN_RESET:
		pm8001_dbg(pm8001_ha, IO, "IO_PORT_IN_RESET\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		break;
	case IO_DS_NON_OPERATIONAL:
		pm8001_dbg(pm8001_ha, IO, "IO_DS_NON_OPERATIONAL\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DEV_NO_RESPONSE;
		break;
	case IO_DS_IN_RECOVERY:
		pm8001_dbg(pm8001_ha, IO, "IO_DS_IN_RECOVERY\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		break;
	case IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY:
		pm8001_dbg(pm8001_ha, IO,
			   "IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY\n");
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		break;
	default:
		pm8001_dbg(pm8001_ha, DEVIO, "Unknown status 0x%x\n", status);
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DEV_NO_RESPONSE;
		/* not allowed case. Therefore, return failed status */
		break;
	}
	spin_lock_irqsave(&t->task_state_lock, flags);
	t->task_state_flags &= ~SAS_TASK_STATE_PENDING;
	t->task_state_flags &= ~SAS_TASK_AT_INITIATOR;
	t->task_state_flags |= SAS_TASK_STATE_DONE;
	if (unlikely((t->task_state_flags & SAS_TASK_STATE_ABORTED))) {
		spin_unlock_irqrestore(&t->task_state_lock, flags);
		pm8001_dbg(pm8001_ha, FAIL,
			   "task 0x%p done with io_status 0x%x resp 0x%xstat 0x%x but aborted by upper layer!\n",
			   t, status, ts->resp, ts->stat);
		pm8001_ccb_task_free(pm8001_ha, t, ccb, tag);
	} else {
		spin_unlock_irqrestore(&t->task_state_lock, flags);
		pm8001_ccb_task_free(pm8001_ha, t, ccb, tag);
		mb();/* in order to force CPU ordering */
		t->task_done(t);
	}
}

/**
 * pm80xx_hw_event_ack_req- For PM8001, some events need to acknowledge to FW.
 * @pm8001_ha: our hba card information
 * @Qnum: the outbound queue message number.
 * @SEA: source of event to ack
 * @port_id: port id.
 * @phyId: phy id.
 * @param0: parameter 0.
 * @param1: parameter 1.
 */
static void pm80xx_hw_event_ack_req(struct pm8001_hba_info *pm8001_ha,
	u32 Qnum, u32 SEA, u32 port_id, u32 phyId, u32 param0, u32 param1)
{
	struct hw_event_ack_req	 payload;
	u32 opc = OPC_INB_SAS_HW_EVENT_ACK;

	struct inbound_queue_table *circularQ;

	memset((u8 *)&payload, 0, sizeof(payload));
	circularQ = &pm8001_ha->inbnd_q_tbl[Qnum];
	payload.tag = cpu_to_le32(1);
	payload.phyid_sea_portid = cpu_to_le32(((SEA & 0xFFFF) << 8) |
		((phyId & 0xFF) << 24) | (port_id & 0xFF));
	payload.param0 = cpu_to_le32(param0);
	payload.param1 = cpu_to_le32(param1);
	pm8001_mpi_build_cmd(pm8001_ha, circularQ, opc, &payload,
			sizeof(payload), 0);
}

static int pm80xx_chip_phy_ctl_req(struct pm8001_hba_info *pm8001_ha,
	u32 phyId, u32 phy_op);

static void hw_event_port_recover(struct pm8001_hba_info *pm8001_ha,
					void *piomb)
{
	struct hw_event_resp *pPayload = (struct hw_event_resp *)(piomb + 4);
	u32 phyid_npip_portstate = le32_to_cpu(pPayload->phyid_npip_portstate);
	u8 phy_id = (u8)((phyid_npip_portstate & 0xFF0000) >> 16);
	u32 lr_status_evt_portid =
		le32_to_cpu(pPayload->lr_status_evt_portid);
	u8 deviceType = pPayload->sas_identify.dev_type;
	u8 link_rate = (u8)((lr_status_evt_portid & 0xF0000000) >> 28);
	struct pm8001_phy *phy = &pm8001_ha->phy[phy_id];
	u8 port_id = (u8)(lr_status_evt_portid & 0x000000FF);
	struct pm8001_port *port = &pm8001_ha->port[port_id];

	if (deviceType == SAS_END_DEVICE) {
		pm80xx_chip_phy_ctl_req(pm8001_ha, phy_id,
					PHY_NOTIFY_ENABLE_SPINUP);
	}

	port->wide_port_phymap |= (1U << phy_id);
	pm8001_get_lrate_mode(phy, link_rate);
	phy->sas_phy.oob_mode = SAS_OOB_MODE;
	phy->phy_state = PHY_STATE_LINK_UP_SPCV;
	phy->phy_attached = 1;
}

/**
 * hw_event_sas_phy_up - FW tells me a SAS phy up event.
 * @pm8001_ha: our hba card information
 * @piomb: IO message buffer
 */
static void
hw_event_sas_phy_up(struct pm8001_hba_info *pm8001_ha, void *piomb)
{
	struct hw_event_resp *pPayload =
		(struct hw_event_resp *)(piomb + 4);
	u32 lr_status_evt_portid =
		le32_to_cpu(pPayload->lr_status_evt_portid);
	u32 phyid_npip_portstate = le32_to_cpu(pPayload->phyid_npip_portstate);

	u8 link_rate =
		(u8)((lr_status_evt_portid & 0xF0000000) >> 28);
	u8 port_id = (u8)(lr_status_evt_portid & 0x000000FF);
	u8 phy_id =
		(u8)((phyid_npip_portstate & 0xFF0000) >> 16);
	u8 portstate = (u8)(phyid_npip_portstate & 0x0000000F);

	struct pm8001_port *port = &pm8001_ha->port[port_id];
	struct pm8001_phy *phy = &pm8001_ha->phy[phy_id];
	unsigned long flags;
	u8 deviceType = pPayload->sas_identify.dev_type;
	phy->port = port;
	port->port_id = port_id;
	port->port_state = portstate;
	port->wide_port_phymap |= (1U << phy_id);
	phy->phy_state = PHY_STATE_LINK_UP_SPCV;
	pm8001_dbg(pm8001_ha, MSG,
		   "portid:%d; phyid:%d; linkrate:%d; portstate:%x; devicetype:%x\n",
		   port_id, phy_id, link_rate, portstate, deviceType);

	switch (deviceType) {
	case SAS_PHY_UNUSED:
		pm8001_dbg(pm8001_ha, MSG, "device type no device.\n");
		break;
	case SAS_END_DEVICE:
		pm8001_dbg(pm8001_ha, MSG, "end device.\n");
		pm80xx_chip_phy_ctl_req(pm8001_ha, phy_id,
			PHY_NOTIFY_ENABLE_SPINUP);
		port->port_attached = 1;
		pm8001_get_lrate_mode(phy, link_rate);
		break;
	case SAS_EDGE_EXPANDER_DEVICE:
		pm8001_dbg(pm8001_ha, MSG, "expander device.\n");
		port->port_attached = 1;
		pm8001_get_lrate_mode(phy, link_rate);
		break;
	case SAS_FANOUT_EXPANDER_DEVICE:
		pm8001_dbg(pm8001_ha, MSG, "fanout expander device.\n");
		port->port_attached = 1;
		pm8001_get_lrate_mode(phy, link_rate);
		break;
	default:
		pm8001_dbg(pm8001_ha, DEVIO, "unknown device type(%x)\n",
			   deviceType);
		break;
	}
	phy->phy_type |= PORT_TYPE_SAS;
	phy->identify.device_type = deviceType;
	phy->phy_attached = 1;
	if (phy->identify.device_type == SAS_END_DEVICE)
		phy->identify.target_port_protocols = SAS_PROTOCOL_SSP;
	else if (phy->identify.device_type != SAS_PHY_UNUSED)
		phy->identify.target_port_protocols = SAS_PROTOCOL_SMP;
	phy->sas_phy.oob_mode = SAS_OOB_MODE;
	sas_notify_phy_event(&phy->sas_phy, PHYE_OOB_DONE, GFP_ATOMIC);
	spin_lock_irqsave(&phy->sas_phy.frame_rcvd_lock, flags);
	memcpy(phy->frame_rcvd, &pPayload->sas_identify,
		sizeof(struct sas_identify_frame)-4);
	phy->frame_rcvd_size = sizeof(struct sas_identify_frame) - 4;
	pm8001_get_attached_sas_addr(phy, phy->sas_phy.attached_sas_addr);
	spin_unlock_irqrestore(&phy->sas_phy.frame_rcvd_lock, flags);
	if (pm8001_ha->flags == PM8001F_RUN_TIME)
		mdelay(200); /* delay a moment to wait for disk to spin up */
	pm8001_bytes_dmaed(pm8001_ha, phy_id);
}

/**
 * hw_event_sata_phy_up - FW tells me a SATA phy up event.
 * @pm8001_ha: our hba card information
 * @piomb: IO message buffer
 */
static void
hw_event_sata_phy_up(struct pm8001_hba_info *pm8001_ha, void *piomb)
{
	struct hw_event_resp *pPayload =
		(struct hw_event_resp *)(piomb + 4);
	u32 phyid_npip_portstate = le32_to_cpu(pPayload->phyid_npip_portstate);
	u32 lr_status_evt_portid =
		le32_to_cpu(pPayload->lr_status_evt_portid);
	u8 link_rate =
		(u8)((lr_status_evt_portid & 0xF0000000) >> 28);
	u8 port_id = (u8)(lr_status_evt_portid & 0x000000FF);
	u8 phy_id =
		(u8)((phyid_npip_portstate & 0xFF0000) >> 16);

	u8 portstate = (u8)(phyid_npip_portstate & 0x0000000F);

	struct pm8001_port *port = &pm8001_ha->port[port_id];
	struct pm8001_phy *phy = &pm8001_ha->phy[phy_id];
	unsigned long flags;
	pm8001_dbg(pm8001_ha, DEVIO,
		   "port id %d, phy id %d link_rate %d portstate 0x%x\n",
		   port_id, phy_id, link_rate, portstate);

	phy->port = port;
	port->port_id = port_id;
	port->port_state = portstate;
	phy->phy_state = PHY_STATE_LINK_UP_SPCV;
	port->port_attached = 1;
	pm8001_get_lrate_mode(phy, link_rate);
	phy->phy_type |= PORT_TYPE_SATA;
	phy->phy_attached = 1;
	phy->sas_phy.oob_mode = SATA_OOB_MODE;
	sas_notify_phy_event(&phy->sas_phy, PHYE_OOB_DONE, GFP_ATOMIC);
	spin_lock_irqsave(&phy->sas_phy.frame_rcvd_lock, flags);
	memcpy(phy->frame_rcvd, ((u8 *)&pPayload->sata_fis - 4),
		sizeof(struct dev_to_host_fis));
	phy->frame_rcvd_size = sizeof(struct dev_to_host_fis);
	phy->identify.target_port_protocols = SAS_PROTOCOL_SATA;
	phy->identify.device_type = SAS_SATA_DEV;
	pm8001_get_attached_sas_addr(phy, phy->sas_phy.attached_sas_addr);
	spin_unlock_irqrestore(&phy->sas_phy.frame_rcvd_lock, flags);
	pm8001_bytes_dmaed(pm8001_ha, phy_id);
}

/**
 * hw_event_phy_down - we should notify the libsas the phy is down.
 * @pm8001_ha: our hba card information
 * @piomb: IO message buffer
 */
static void
hw_event_phy_down(struct pm8001_hba_info *pm8001_ha, void *piomb)
{
	struct hw_event_resp *pPayload =
		(struct hw_event_resp *)(piomb + 4);

	u32 lr_status_evt_portid =
		le32_to_cpu(pPayload->lr_status_evt_portid);
	u8 port_id = (u8)(lr_status_evt_portid & 0x000000FF);
	u32 phyid_npip_portstate = le32_to_cpu(pPayload->phyid_npip_portstate);
	u8 phy_id =
		(u8)((phyid_npip_portstate & 0xFF0000) >> 16);
	u8 portstate = (u8)(phyid_npip_portstate & 0x0000000F);

	struct pm8001_port *port = &pm8001_ha->port[port_id];
	struct pm8001_phy *phy = &pm8001_ha->phy[phy_id];
	u32 port_sata = (phy->phy_type & PORT_TYPE_SATA);
	port->port_state = portstate;
	phy->identify.device_type = 0;
	phy->phy_attached = 0;
	switch (portstate) {
	case PORT_VALID:
		break;
	case PORT_INVALID:
		pm8001_dbg(pm8001_ha, MSG, " PortInvalid portID %d\n",
			   port_id);
		pm8001_dbg(pm8001_ha, MSG,
			   " Last phy Down and port invalid\n");
		if (port_sata) {
			phy->phy_type = 0;
			port->port_attached = 0;
			pm80xx_hw_event_ack_req(pm8001_ha, 0, HW_EVENT_PHY_DOWN,
					port_id, phy_id, 0, 0);
		}
		sas_phy_disconnected(&phy->sas_phy);
		break;
	case PORT_IN_RESET:
		pm8001_dbg(pm8001_ha, MSG, " Port In Reset portID %d\n",
			   port_id);
		break;
	case PORT_NOT_ESTABLISHED:
		pm8001_dbg(pm8001_ha, MSG,
			   " Phy Down and PORT_NOT_ESTABLISHED\n");
		port->port_attached = 0;
		break;
	case PORT_LOSTCOMM:
		pm8001_dbg(pm8001_ha, MSG, " Phy Down and PORT_LOSTCOMM\n");
		pm8001_dbg(pm8001_ha, MSG,
			   " Last phy Down and port invalid\n");
		if (port_sata) {
			port->port_attached = 0;
			phy->phy_type = 0;
			pm80xx_hw_event_ack_req(pm8001_ha, 0, HW_EVENT_PHY_DOWN,
					port_id, phy_id, 0, 0);
		}
		sas_phy_disconnected(&phy->sas_phy);
		break;
	default:
		port->port_attached = 0;
		pm8001_dbg(pm8001_ha, DEVIO,
			   " Phy Down and(default) = 0x%x\n",
			   portstate);
		break;

	}
	if (port_sata && (portstate != PORT_IN_RESET))
		sas_notify_phy_event(&phy->sas_phy, PHYE_LOSS_OF_SIGNAL,
				GFP_ATOMIC);
}

static int mpi_phy_start_resp(struct pm8001_hba_info *pm8001_ha, void *piomb)
{
	struct phy_start_resp *pPayload =
		(struct phy_start_resp *)(piomb + 4);
	u32 status =
		le32_to_cpu(pPayload->status);
	u32 phy_id =
		le32_to_cpu(pPayload->phyid) & 0xFF;
	struct pm8001_phy *phy = &pm8001_ha->phy[phy_id];

	pm8001_dbg(pm8001_ha, INIT,
		   "phy start resp status:0x%x, phyid:0x%x\n",
		   status, phy_id);
	if (status == 0)
		phy->phy_state = PHY_LINK_DOWN;

	if (pm8001_ha->flags == PM8001F_RUN_TIME &&
			phy->enable_completion != NULL) {
		complete(phy->enable_completion);
		phy->enable_completion = NULL;
	}
	return 0;

}

/**
 * mpi_thermal_hw_event - a thermal hw event has come.
 * @pm8001_ha: our hba card information
 * @piomb: IO message buffer
 */
static int mpi_thermal_hw_event(struct pm8001_hba_info *pm8001_ha, void *piomb)
{
	struct thermal_hw_event *pPayload =
		(struct thermal_hw_event *)(piomb + 4);

	u32 thermal_event = le32_to_cpu(pPayload->thermal_event);
	u32 rht_lht = le32_to_cpu(pPayload->rht_lht);

	if (thermal_event & 0x40) {
		pm8001_dbg(pm8001_ha, IO,
			   "Thermal Event: Local high temperature violated!\n");
		pm8001_dbg(pm8001_ha, IO,
			   "Thermal Event: Measured local high temperature %d\n",
			   ((rht_lht & 0xFF00) >> 8));
	}
	if (thermal_event & 0x10) {
		pm8001_dbg(pm8001_ha, IO,
			   "Thermal Event: Remote high temperature violated!\n");
		pm8001_dbg(pm8001_ha, IO,
			   "Thermal Event: Measured remote high temperature %d\n",
			   ((rht_lht & 0xFF000000) >> 24));
	}
	return 0;
}

/**
 * mpi_hw_event - The hw event has come.
 * @pm8001_ha: our hba card information
 * @piomb: IO message buffer
 */
static int mpi_hw_event(struct pm8001_hba_info *pm8001_ha, void *piomb)
{
	unsigned long flags, i;
	struct hw_event_resp *pPayload =
		(struct hw_event_resp *)(piomb + 4);
	u32 lr_status_evt_portid =
		le32_to_cpu(pPayload->lr_status_evt_portid);
	u32 phyid_npip_portstate = le32_to_cpu(pPayload->phyid_npip_portstate);
	u8 port_id = (u8)(lr_status_evt_portid & 0x000000FF);
	u8 phy_id =
		(u8)((phyid_npip_portstate & 0xFF0000) >> 16);
	u16 eventType =
		(u16)((lr_status_evt_portid & 0x00FFFF00) >> 8);
	u8 status =
		(u8)((lr_status_evt_portid & 0x0F000000) >> 24);
	struct sas_ha_struct *sas_ha = pm8001_ha->sas;
	struct pm8001_phy *phy = &pm8001_ha->phy[phy_id];
	struct pm8001_port *port = &pm8001_ha->port[port_id];
	struct asd_sas_phy *sas_phy = sas_ha->sas_phy[phy_id];
	pm8001_dbg(pm8001_ha, DEV,
		   "portid:%d phyid:%d event:0x%x status:0x%x\n",
		   port_id, phy_id, eventType, status);

	switch (eventType) {

	case HW_EVENT_SAS_PHY_UP:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_PHY_START_STATUS\n");
		hw_event_sas_phy_up(pm8001_ha, piomb);
		break;
	case HW_EVENT_SATA_PHY_UP:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_SATA_PHY_UP\n");
		hw_event_sata_phy_up(pm8001_ha, piomb);
		break;
	case HW_EVENT_SATA_SPINUP_HOLD:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_SATA_SPINUP_HOLD\n");
		sas_notify_phy_event(&phy->sas_phy, PHYE_SPINUP_HOLD,
			GFP_ATOMIC);
		break;
	case HW_EVENT_PHY_DOWN:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_PHY_DOWN\n");
		hw_event_phy_down(pm8001_ha, piomb);
		if (pm8001_ha->reset_in_progress) {
			pm8001_dbg(pm8001_ha, MSG, "Reset in progress\n");
			return 0;
		}
		phy->phy_attached = 0;
		phy->phy_state = PHY_LINK_DISABLE;
		break;
	case HW_EVENT_PORT_INVALID:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_PORT_INVALID\n");
		sas_phy_disconnected(sas_phy);
		phy->phy_attached = 0;
		sas_notify_port_event(sas_phy, PORTE_LINK_RESET_ERR,
			GFP_ATOMIC);
		break;
	/* the broadcast change primitive received, tell the LIBSAS this event
	to revalidate the sas domain*/
	case HW_EVENT_BROADCAST_CHANGE:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_BROADCAST_CHANGE\n");
		pm80xx_hw_event_ack_req(pm8001_ha, 0, HW_EVENT_BROADCAST_CHANGE,
			port_id, phy_id, 1, 0);
		spin_lock_irqsave(&sas_phy->sas_prim_lock, flags);
		sas_phy->sas_prim = HW_EVENT_BROADCAST_CHANGE;
		spin_unlock_irqrestore(&sas_phy->sas_prim_lock, flags);
		sas_notify_port_event(sas_phy, PORTE_BROADCAST_RCVD,
			GFP_ATOMIC);
		break;
	case HW_EVENT_PHY_ERROR:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_PHY_ERROR\n");
		sas_phy_disconnected(&phy->sas_phy);
		phy->phy_attached = 0;
		sas_notify_phy_event(&phy->sas_phy, PHYE_OOB_ERROR, GFP_ATOMIC);
		break;
	case HW_EVENT_BROADCAST_EXP:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_BROADCAST_EXP\n");
		spin_lock_irqsave(&sas_phy->sas_prim_lock, flags);
		sas_phy->sas_prim = HW_EVENT_BROADCAST_EXP;
		spin_unlock_irqrestore(&sas_phy->sas_prim_lock, flags);
		sas_notify_port_event(sas_phy, PORTE_BROADCAST_RCVD,
			GFP_ATOMIC);
		break;
	case HW_EVENT_LINK_ERR_INVALID_DWORD:
		pm8001_dbg(pm8001_ha, MSG,
			   "HW_EVENT_LINK_ERR_INVALID_DWORD\n");
		pm80xx_hw_event_ack_req(pm8001_ha, 0,
			HW_EVENT_LINK_ERR_INVALID_DWORD, port_id, phy_id, 0, 0);
		break;
	case HW_EVENT_LINK_ERR_DISPARITY_ERROR:
		pm8001_dbg(pm8001_ha, MSG,
			   "HW_EVENT_LINK_ERR_DISPARITY_ERROR\n");
		pm80xx_hw_event_ack_req(pm8001_ha, 0,
			HW_EVENT_LINK_ERR_DISPARITY_ERROR,
			port_id, phy_id, 0, 0);
		break;
	case HW_EVENT_LINK_ERR_CODE_VIOLATION:
		pm8001_dbg(pm8001_ha, MSG,
			   "HW_EVENT_LINK_ERR_CODE_VIOLATION\n");
		pm80xx_hw_event_ack_req(pm8001_ha, 0,
			HW_EVENT_LINK_ERR_CODE_VIOLATION,
			port_id, phy_id, 0, 0);
		break;
	case HW_EVENT_LINK_ERR_LOSS_OF_DWORD_SYNCH:
		pm8001_dbg(pm8001_ha, MSG,
			   "HW_EVENT_LINK_ERR_LOSS_OF_DWORD_SYNCH\n");
		pm80xx_hw_event_ack_req(pm8001_ha, 0,
			HW_EVENT_LINK_ERR_LOSS_OF_DWORD_SYNCH,
			port_id, phy_id, 0, 0);
		break;
	case HW_EVENT_MALFUNCTION:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_MALFUNCTION\n");
		break;
	case HW_EVENT_BROADCAST_SES:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_BROADCAST_SES\n");
		spin_lock_irqsave(&sas_phy->sas_prim_lock, flags);
		sas_phy->sas_prim = HW_EVENT_BROADCAST_SES;
		spin_unlock_irqrestore(&sas_phy->sas_prim_lock, flags);
		sas_notify_port_event(sas_phy, PORTE_BROADCAST_RCVD,
			GFP_ATOMIC);
		break;
	case HW_EVENT_INBOUND_CRC_ERROR:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_INBOUND_CRC_ERROR\n");
		pm80xx_hw_event_ack_req(pm8001_ha, 0,
			HW_EVENT_INBOUND_CRC_ERROR,
			port_id, phy_id, 0, 0);
		break;
	case HW_EVENT_HARD_RESET_RECEIVED:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_HARD_RESET_RECEIVED\n");
		sas_notify_port_event(sas_phy, PORTE_HARD_RESET, GFP_ATOMIC);
		break;
	case HW_EVENT_ID_FRAME_TIMEOUT:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_ID_FRAME_TIMEOUT\n");
		sas_phy_disconnected(sas_phy);
		phy->phy_attached = 0;
		sas_notify_port_event(sas_phy, PORTE_LINK_RESET_ERR,
			GFP_ATOMIC);
		break;
	case HW_EVENT_LINK_ERR_PHY_RESET_FAILED:
		pm8001_dbg(pm8001_ha, MSG,
			   "HW_EVENT_LINK_ERR_PHY_RESET_FAILED\n");
		pm80xx_hw_event_ack_req(pm8001_ha, 0,
			HW_EVENT_LINK_ERR_PHY_RESET_FAILED,
			port_id, phy_id, 0, 0);
		sas_phy_disconnected(sas_phy);
		phy->phy_attached = 0;
		sas_notify_port_event(sas_phy, PORTE_LINK_RESET_ERR,
			GFP_ATOMIC);
		break;
	case HW_EVENT_PORT_RESET_TIMER_TMO:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_PORT_RESET_TIMER_TMO\n");
		if (!pm8001_ha->phy[phy_id].reset_completion) {
			pm80xx_hw_event_ack_req(pm8001_ha, 0, HW_EVENT_PHY_DOWN,
				port_id, phy_id, 0, 0);
		}
		sas_phy_disconnected(sas_phy);
		phy->phy_attached = 0;
		sas_notify_port_event(sas_phy, PORTE_LINK_RESET_ERR,
			GFP_ATOMIC);
		if (pm8001_ha->phy[phy_id].reset_completion) {
			pm8001_ha->phy[phy_id].port_reset_status =
					PORT_RESET_TMO;
			complete(pm8001_ha->phy[phy_id].reset_completion);
			pm8001_ha->phy[phy_id].reset_completion = NULL;
		}
		break;
	case HW_EVENT_PORT_RECOVERY_TIMER_TMO:
		pm8001_dbg(pm8001_ha, MSG,
			   "HW_EVENT_PORT_RECOVERY_TIMER_TMO\n");
		pm80xx_hw_event_ack_req(pm8001_ha, 0,
			HW_EVENT_PORT_RECOVERY_TIMER_TMO,
			port_id, phy_id, 0, 0);
		for (i = 0; i < pm8001_ha->chip->n_phy; i++) {
			if (port->wide_port_phymap & (1 << i)) {
				phy = &pm8001_ha->phy[i];
				sas_notify_phy_event(&phy->sas_phy,
					PHYE_LOSS_OF_SIGNAL, GFP_ATOMIC);
				port->wide_port_phymap &= ~(1 << i);
			}
		}
		break;
	case HW_EVENT_PORT_RECOVER:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_PORT_RECOVER\n");
		hw_event_port_recover(pm8001_ha, piomb);
		break;
	case HW_EVENT_PORT_RESET_COMPLETE:
		pm8001_dbg(pm8001_ha, MSG, "HW_EVENT_PORT_RESET_COMPLETE\n");
		if (pm8001_ha->phy[phy_id].reset_completion) {
			pm8001_ha->phy[phy_id].port_reset_status =
					PORT_RESET_SUCCESS;
			complete(pm8001_ha->phy[phy_id].reset_completion);
			pm8001_ha->phy[phy_id].reset_completion = NULL;
		}
		break;
	case EVENT_BROADCAST_ASYNCH_EVENT:
		pm8001_dbg(pm8001_ha, MSG, "EVENT_BROADCAST_ASYNCH_EVENT\n");
		break;
	default:
		pm8001_dbg(pm8001_ha, DEVIO, "Unknown event type 0x%x\n",
			   eventType);
		break;
	}
	return 0;
}

/**
 * mpi_phy_stop_resp - SPCv specific
 * @pm8001_ha: our hba card information
 * @piomb: IO message buffer
 */
static int mpi_phy_stop_resp(struct pm8001_hba_info *pm8001_ha, void *piomb)
{
	struct phy_stop_resp *pPayload =
		(struct phy_stop_resp *)(piomb + 4);
	u32 status =
		le32_to_cpu(pPayload->status);
	u32 phyid =
		le32_to_cpu(pPayload->phyid) & 0xFF;
	struct pm8001_phy *phy = &pm8001_ha->phy[phyid];
	pm8001_dbg(pm8001_ha, MSG, "phy:0x%x status:0x%x\n",
		   phyid, status);
	if (status == PHY_STOP_SUCCESS ||
		status == PHY_STOP_ERR_DEVICE_ATTACHED)
		phy->phy_state = PHY_LINK_DISABLE;
	return 0;
}

/**
 * mpi_set_controller_config_resp - SPCv specific
 * @pm8001_ha: our hba card information
 * @piomb: IO message buffer
 */
static int mpi_set_controller_config_resp(struct pm8001_hba_info *pm8001_ha,
			void *piomb)
{
	struct set_ctrl_cfg_resp *pPayload =
			(struct set_ctrl_cfg_resp *)(piomb + 4);
	u32 status = le32_to_cpu(pPayload->status);
	u32 err_qlfr_pgcd = le32_to_cpu(pPayload->err_qlfr_pgcd);

	pm8001_dbg(pm8001_ha, MSG,
		   "SET CONTROLLER RESP: status 0x%x qlfr_pgcd 0x%x\n",
		   status, err_qlfr_pgcd);

	return 0;
}

/**
 * mpi_get_controller_config_resp - SPCv specific
 * @pm8001_ha: our hba card information
 * @piomb: IO message buffer
 */
static int mpi_get_controller_config_resp(struct pm8001_hba_info *pm8001_ha,
			void *piomb)
{
	pm8001_dbg(pm8001_ha, MSG, " pm80xx_addition_functionality\n");

	return 0;
}

/**
 * mpi_get_phy_profile_resp - SPCv specific
 * @pm8001_ha: our hba card information
 * @piomb: IO message buffer
 */
static int mpi_get_phy_profile_resp(struct pm8001_hba_info *pm8001_ha,
			void *piomb)
{
	pm8001_dbg(pm8001_ha, MSG, " pm80xx_addition_functionality\n");

	return 0;
}

/**
 * mpi_flash_op_ext_resp - SPCv specific
 * @pm8001_ha: our hba card information
 * @piomb: IO message buffer
 */
static int mpi_flash_op_ext_resp(struct pm8001_hba_info *pm8001_ha, void *piomb)
{
	pm8001_dbg(pm8001_ha, MSG, " pm80xx_addition_functionality\n");

	return 0;
}

/**
 * mpi_set_phy_profile_resp - SPCv specific
 * @pm8001_ha: our hba card information
 * @piomb: IO message buffer
 */
static int mpi_set_phy_profile_resp(struct pm8001_hba_info *pm8001_ha,
			void *piomb)
{
	u32 tag;
	u8 page_code;
	int rc = 0;
	struct set_phy_profile_resp *pPayload =
		(struct set_phy_profile_resp *)(piomb + 4);
	u32 ppc_phyid = le32_to_cpu(pPayload->ppc_phyid);
	u32 status = le32_to_cpu(pPayload->status);

	tag = le32_to_cpu(pPayload->tag);
	page_code = (u8)((ppc_phyid & 0xFF00) >> 8);
	if (status) {
		/* status is FAILED */
		pm8001_dbg(pm8001_ha, FAIL,
			   "PhyProfile command failed  with status 0x%08X\n",
			   status);
		rc = -1;
	} else {
		if (page_code != SAS_PHY_ANALOG_SETTINGS_PAGE) {
			pm8001_dbg(pm8001_ha, FAIL, "Invalid page code 0x%X\n",
				   page_code);
			rc = -1;
		}
	}
	pm8001_tag_free(pm8001_ha, tag);
	return rc;
}

/**
 * mpi_kek_management_resp - SPCv specific
 * @pm8001_ha: our hba card information
 * @piomb: IO message buffer
 */
static int mpi_kek_management_resp(struct pm8001_hba_info *pm8001_ha,
			void *piomb)
{
	struct kek_mgmt_resp *pPayload = (struct kek_mgmt_resp *)(piomb + 4);

	u32 status = le32_to_cpu(pPayload->status);
	u32 kidx_new_curr_ksop = le32_to_cpu(pPayload->kidx_new_curr_ksop);
	u32 err_qlfr = le32_to_cpu(pPayload->err_qlfr);

	pm8001_dbg(pm8001_ha, MSG,
		   "KEK MGMT RESP. Status 0x%x idx_ksop 0x%x err_qlfr 0x%x\n",
		   status, kidx_new_curr_ksop, err_qlfr);

	return 0;
}

/**
 * mpi_dek_management_resp - SPCv specific
 * @pm8001_ha: our hba card information
 * @piomb: IO message buffer
 */
static int mpi_dek_management_resp(struct pm8001_hba_info *pm8001_ha,
			void *piomb)
{
	pm8001_dbg(pm8001_ha, MSG, " pm80xx_addition_functionality\n");

	return 0;
}

/**
 * ssp_coalesced_comp_resp - SPCv specific
 * @pm8001_ha: our hba card information
 * @piomb: IO message buffer
 */
static int ssp_coalesced_comp_resp(struct pm8001_hba_info *pm8001_ha,
			void *piomb)
{
	pm8001_dbg(pm8001_ha, MSG, " pm80xx_addition_functionality\n");

	return 0;
}

/**
 * process_one_iomb - process one outbound Queue memory block
 * @pm8001_ha: our hba card information
 * @circularQ: outbound circular queue
 * @piomb: IO message buffer
 */
static void process_one_iomb(struct pm8001_hba_info *pm8001_ha,
		struct outbound_queue_table *circularQ, void *piomb)
{
	__le32 pHeader = *(__le32 *)piomb;
	u32 opc = (u32)((le32_to_cpu(pHeader)) & 0xFFF);

	switch (opc) {
	case OPC_OUB_ECHO:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_ECHO\n");
		break;
	case OPC_OUB_HW_EVENT:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_HW_EVENT\n");
		mpi_hw_event(pm8001_ha, piomb);
		break;
	case OPC_OUB_THERM_HW_EVENT:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_THERMAL_EVENT\n");
		mpi_thermal_hw_event(pm8001_ha, piomb);
		break;
	case OPC_OUB_SSP_COMP:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_SSP_COMP\n");
		mpi_ssp_completion(pm8001_ha, piomb);
		break;
	case OPC_OUB_SMP_COMP:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_SMP_COMP\n");
		mpi_smp_completion(pm8001_ha, piomb);
		break;
	case OPC_OUB_LOCAL_PHY_CNTRL:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_LOCAL_PHY_CNTRL\n");
		pm8001_mpi_local_phy_ctl(pm8001_ha, piomb);
		break;
	case OPC_OUB_DEV_REGIST:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_DEV_REGIST\n");
		pm8001_mpi_reg_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_DEREG_DEV:
		pm8001_dbg(pm8001_ha, MSG, "unregister the device\n");
		pm8001_mpi_dereg_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_GET_DEV_HANDLE:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_GET_DEV_HANDLE\n");
		break;
	case OPC_OUB_SATA_COMP:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_SATA_COMP\n");
		mpi_sata_completion(pm8001_ha, circularQ, piomb);
		break;
	case OPC_OUB_SATA_EVENT:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_SATA_EVENT\n");
		mpi_sata_event(pm8001_ha, circularQ, piomb);
		break;
	case OPC_OUB_SSP_EVENT:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_SSP_EVENT\n");
		mpi_ssp_event(pm8001_ha, piomb);
		break;
	case OPC_OUB_DEV_HANDLE_ARRIV:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_DEV_HANDLE_ARRIV\n");
		/*This is for target*/
		break;
	case OPC_OUB_SSP_RECV_EVENT:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_SSP_RECV_EVENT\n");
		/*This is for target*/
		break;
	case OPC_OUB_FW_FLASH_UPDATE:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_FW_FLASH_UPDATE\n");
		pm8001_mpi_fw_flash_update_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_GPIO_RESPONSE:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_GPIO_RESPONSE\n");
		break;
	case OPC_OUB_GPIO_EVENT:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_GPIO_EVENT\n");
		break;
	case OPC_OUB_GENERAL_EVENT:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_GENERAL_EVENT\n");
		pm8001_mpi_general_event(pm8001_ha, piomb);
		break;
	case OPC_OUB_SSP_ABORT_RSP:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_SSP_ABORT_RSP\n");
		pm8001_mpi_task_abort_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_SATA_ABORT_RSP:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_SATA_ABORT_RSP\n");
		pm8001_mpi_task_abort_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_SAS_DIAG_MODE_START_END:
		pm8001_dbg(pm8001_ha, MSG,
			   "OPC_OUB_SAS_DIAG_MODE_START_END\n");
		break;
	case OPC_OUB_SAS_DIAG_EXECUTE:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_SAS_DIAG_EXECUTE\n");
		break;
	case OPC_OUB_GET_TIME_STAMP:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_GET_TIME_STAMP\n");
		break;
	case OPC_OUB_SAS_HW_EVENT_ACK:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_SAS_HW_EVENT_ACK\n");
		break;
	case OPC_OUB_PORT_CONTROL:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_PORT_CONTROL\n");
		break;
	case OPC_OUB_SMP_ABORT_RSP:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_SMP_ABORT_RSP\n");
		pm8001_mpi_task_abort_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_GET_NVMD_DATA:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_GET_NVMD_DATA\n");
		pm8001_mpi_get_nvmd_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_SET_NVMD_DATA:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_SET_NVMD_DATA\n");
		pm8001_mpi_set_nvmd_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_DEVICE_HANDLE_REMOVAL:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_DEVICE_HANDLE_REMOVAL\n");
		break;
	case OPC_OUB_SET_DEVICE_STATE:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_SET_DEVICE_STATE\n");
		pm8001_mpi_set_dev_state_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_GET_DEVICE_STATE:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_GET_DEVICE_STATE\n");
		break;
	case OPC_OUB_SET_DEV_INFO:
		pm8001_dbg(pm8001_ha, MSG, "OPC_OUB_SET_DEV_INFO\n");
		break;
	/* spcv specific commands */
	case OPC_OUB_PHY_START_RESP:
		pm8001_dbg(pm8001_ha, MSG,
			   "OPC_OUB_PHY_START_RESP opcode:%x\n", opc);
		mpi_phy_start_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_PHY_STOP_RESP:
		pm8001_dbg(pm8001_ha, MSG,
			   "OPC_OUB_PHY_STOP_RESP opcode:%x\n", opc);
		mpi_phy_stop_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_SET_CONTROLLER_CONFIG:
		pm8001_dbg(pm8001_ha, MSG,
			   "OPC_OUB_SET_CONTROLLER_CONFIG opcode:%x\n", opc);
		mpi_set_controller_config_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_GET_CONTROLLER_CONFIG:
		pm8001_dbg(pm8001_ha, MSG,
			   "OPC_OUB_GET_CONTROLLER_CONFIG opcode:%x\n", opc);
		mpi_get_controller_config_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_GET_PHY_PROFILE:
		pm8001_dbg(pm8001_ha, MSG,
			   "OPC_OUB_GET_PHY_PROFILE opcode:%x\n", opc);
		mpi_get_phy_profile_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_FLASH_OP_EXT:
		pm8001_dbg(pm8001_ha, MSG,
			   "OPC_OUB_FLASH_OP_EXT opcode:%x\n", opc);
		mpi_flash_op_ext_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_SET_PHY_PROFILE:
		pm8001_dbg(pm8001_ha, MSG,
			   "OPC_OUB_SET_PHY_PROFILE opcode:%x\n", opc);
		mpi_set_phy_profile_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_KEK_MANAGEMENT_RESP:
		pm8001_dbg(pm8001_ha, MSG,
			   "OPC_OUB_KEK_MANAGEMENT_RESP opcode:%x\n", opc);
		mpi_kek_management_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_DEK_MANAGEMENT_RESP:
		pm8001_dbg(pm8001_ha, MSG,
			   "OPC_OUB_DEK_MANAGEMENT_RESP opcode:%x\n", opc);
		mpi_dek_management_resp(pm8001_ha, piomb);
		break;
	case OPC_OUB_SSP_COALESCED_COMP_RESP:
		pm8001_dbg(pm8001_ha, MSG,
			   "OPC_OUB_SSP_COALESCED_COMP_RESP opcode:%x\n", opc);
		ssp_coalesced_comp_resp(pm8001_ha, piomb);
		break;
	default:
		pm8001_dbg(pm8001_ha, DEVIO,
			   "Unknown outbound Queue IOMB OPC = 0x%x\n", opc);
		break;
	}
}

static void print_scratchpad_registers(struct pm8001_hba_info *pm8001_ha)
{
	pm8001_dbg(pm8001_ha, FAIL, "MSGU_SCRATCH_PAD_0: 0x%x\n",
		   pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_0));
	pm8001_dbg(pm8001_ha, FAIL, "MSGU_SCRATCH_PAD_1:0x%x\n",
		   pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_1));
	pm8001_dbg(pm8001_ha, FAIL, "MSGU_SCRATCH_PAD_2: 0x%x\n",
		   pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_2));
	pm8001_dbg(pm8001_ha, FAIL, "MSGU_SCRATCH_PAD_3: 0x%x\n",
		   pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_3));
	pm8001_dbg(pm8001_ha, FAIL, "MSGU_HOST_SCRATCH_PAD_0: 0x%x\n",
		   pm8001_cr32(pm8001_ha, 0, MSGU_HOST_SCRATCH_PAD_0));
	pm8001_dbg(pm8001_ha, FAIL, "MSGU_HOST_SCRATCH_PAD_1: 0x%x\n",
		   pm8001_cr32(pm8001_ha, 0, MSGU_HOST_SCRATCH_PAD_1));
	pm8001_dbg(pm8001_ha, FAIL, "MSGU_HOST_SCRATCH_PAD_2: 0x%x\n",
		   pm8001_cr32(pm8001_ha, 0, MSGU_HOST_SCRATCH_PAD_2));
	pm8001_dbg(pm8001_ha, FAIL, "MSGU_HOST_SCRATCH_PAD_3: 0x%x\n",
		   pm8001_cr32(pm8001_ha, 0, MSGU_HOST_SCRATCH_PAD_3));
	pm8001_dbg(pm8001_ha, FAIL, "MSGU_HOST_SCRATCH_PAD_4: 0x%x\n",
		   pm8001_cr32(pm8001_ha, 0, MSGU_HOST_SCRATCH_PAD_4));
	pm8001_dbg(pm8001_ha, FAIL, "MSGU_HOST_SCRATCH_PAD_5: 0x%x\n",
		   pm8001_cr32(pm8001_ha, 0, MSGU_HOST_SCRATCH_PAD_5));
	pm8001_dbg(pm8001_ha, FAIL, "MSGU_RSVD_SCRATCH_PAD_0: 0x%x\n",
		   pm8001_cr32(pm8001_ha, 0, MSGU_HOST_SCRATCH_PAD_6));
	pm8001_dbg(pm8001_ha, FAIL, "MSGU_RSVD_SCRATCH_PAD_1: 0x%x\n",
		   pm8001_cr32(pm8001_ha, 0, MSGU_HOST_SCRATCH_PAD_7));
}

static int process_oq(struct pm8001_hba_info *pm8001_ha, u8 vec)
{
	struct outbound_queue_table *circularQ;
	void *pMsg1 = NULL;
	u8 bc;
	u32 ret = MPI_IO_STATUS_FAIL;
	u32 regval;

	/*
	 * Fatal errors are programmed to be signalled in irq vector
	 * pm8001_ha->max_q_num - 1 through pm8001_ha->main_cfg_tbl.pm80xx_tbl.
	 * fatal_err_interrupt
	 */
	if (vec == (pm8001_ha->max_q_num - 1)) {
		u32 mipsall_ready;

		if (pm8001_ha->chip_id == chip_8008 ||
		    pm8001_ha->chip_id == chip_8009)
			mipsall_ready = SCRATCH_PAD_MIPSALL_READY_8PORT;
		else
			mipsall_ready = SCRATCH_PAD_MIPSALL_READY_16PORT;

		regval = pm8001_cr32(pm8001_ha, 0, MSGU_SCRATCH_PAD_1);
		if ((regval & mipsall_ready) != mipsall_ready) {
			pm8001_ha->controller_fatal_error = true;
			pm8001_dbg(pm8001_ha, FAIL,
				   "Firmware Fatal error! Regval:0x%x\n",
				   regval);
			pm8001_handle_event(pm8001_ha, NULL, IO_FATAL_ERROR);
			print_scratchpad_registers(pm8001_ha);
			return ret;
		}
	}
	circularQ = &pm8001_ha->outbnd_q_tbl[vec];
	spin_lock_irqsave(&circularQ->oq_lock, circularQ->lock_flags);
	do {
		/* spurious interrupt during setup if kexec-ing and
		 * driver doing a doorbell access w/ the pre-kexec oq
		 * interrupt setup.
		 */
		if (!circularQ->pi_virt)
			break;
		ret = pm8001_mpi_msg_consume(pm8001_ha, circularQ, &pMsg1, &bc);
		if (MPI_IO_STATUS_SUCCESS == ret) {
			/* process the outbound message */
			process_one_iomb(pm8001_ha, circularQ,
						(void *)(pMsg1 - 4));
			/* free the message from the outbound circular buffer */
			pm8001_mpi_msg_free_set(pm8001_ha, pMsg1,
							circularQ, bc);
		}
		if (MPI_IO_STATUS_BUSY == ret) {
			/* Update the producer index from SPC */
			circularQ->producer_index =
				cpu_to_le32(pm8001_read_32(circularQ->pi_virt));
			if (le32_to_cpu(circularQ->producer_index) ==
				circularQ->consumer_idx)
				/* OQ is empty */
				break;
		}
	} while (1);
	spin_unlock_irqrestore(&circularQ->oq_lock, circularQ->lock_flags);
	return ret;
}

/* DMA_... to our direction translation. */
static const u8 data_dir_flags[] = {
	[DMA_BIDIRECTIONAL]	= DATA_DIR_BYRECIPIENT,	/* UNSPECIFIED */
	[DMA_TO_DEVICE]		= DATA_DIR_OUT,		/* OUTBOUND */
	[DMA_FROM_DEVICE]	= DATA_DIR_IN,		/* INBOUND */
	[DMA_NONE]		= DATA_DIR_NONE,	/* NO TRANSFER */
};

static void build_smp_cmd(u32 deviceID, __le32 hTag,
			struct smp_req *psmp_cmd, int mode, int length)
{
	psmp_cmd->tag = hTag;
	psmp_cmd->device_id = cpu_to_le32(deviceID);
	if (mode == SMP_DIRECT) {
		length = length - 4; /* subtract crc */
		psmp_cmd->len_ip_ir = cpu_to_le32(length << 16);
	} else {
		psmp_cmd->len_ip_ir = cpu_to_le32(1|(1 << 1));
	}
}

/**
 * pm80xx_chip_smp_req - send an SMP task to FW
 * @pm8001_ha: our hba card information.
 * @ccb: the ccb information this request used.
 */
static int pm80xx_chip_smp_req(struct pm8001_hba_info *pm8001_ha,
	struct pm8001_ccb_info *ccb)
{
	int elem, rc;
	struct sas_task *task = ccb->task;
	struct domain_device *dev = task->dev;
	struct pm8001_device *pm8001_dev = dev->lldd_dev;
	struct scatterlist *sg_req, *sg_resp, *smp_req;
	u32 req_len, resp_len;
	struct smp_req smp_cmd;
	u32 opc;
	struct inbound_queue_table *circularQ;
	u32 i, length;
	u8 *payload;
	u8 *to;

	memset(&smp_cmd, 0, sizeof(smp_cmd));
	/*
	 * DMA-map SMP request, response buffers
	 */
	sg_req = &task->smp_task.smp_req;
	elem = dma_map_sg(pm8001_ha->dev, sg_req, 1, DMA_TO_DEVICE);
	if (!elem)
		return -ENOMEM;
	req_len = sg_dma_len(sg_req);

	sg_resp = &task->smp_task.smp_resp;
	elem = dma_map_sg(pm8001_ha->dev, sg_resp, 1, DMA_FROM_DEVICE);
	if (!elem) {
		rc = -ENOMEM;
		goto err_out;
	}
	resp_len = sg_dma_len(sg_resp);
	/* must be in dwords */
	if ((req_len & 0x3) || (resp_len & 0x3)) {
		rc = -EINVAL;
		goto err_out_2;
	}

	opc = OPC_INB_SMP_REQUEST;
	circularQ = &pm8001_ha->inbnd_q_tbl[0];
	smp_cmd.tag = cpu_to_le32(ccb->ccb_tag);

	length = sg_req->length;
	pm8001_dbg(pm8001_ha, IO, "SMP Frame Length %d\n", sg_req->length);
	if (!(length - 8))
		pm8001_ha->smp_exp_mode = SMP_DIRECT;
	else
		pm8001_ha->smp_exp_mode = SMP_INDIRECT;


	smp_req = &task->smp_task.smp_req;
	to = kmap_atomic(sg_page(smp_req));
	payload = to + smp_req->offset;

	/* INDIRECT MODE command settings. Use DMA */
	if (pm8001_ha->smp_exp_mode == SMP_INDIRECT) {
		pm8001_dbg(pm8001_ha, IO, "SMP REQUEST INDIRECT MODE\n");
		/* for SPCv indirect mode. Place the top 4 bytes of
		 * SMP Request header here. */
		for (i = 0; i < 4; i++)
			smp_cmd.smp_req16[i] = *(payload + i);
		/* exclude top 4 bytes for SMP req header */
		smp_cmd.long_smp_req.long_req_addr =
			cpu_to_le64((u64)sg_dma_address
				(&task->smp_task.smp_req) + 4);
		/* exclude 4 bytes for SMP req header and CRC */
		smp_cmd.long_smp_req.long_req_size =
			cpu_to_le32((u32)sg_dma_len(&task->smp_task.smp_req)-8);
		smp_cmd.long_smp_req.long_resp_addr =
				cpu_to_le64((u64)sg_dma_address
					(&task->smp_task.smp_resp));
		smp_cmd.long_smp_req.long_resp_size =
				cpu_to_le32((u32)sg_dma_len
					(&task->smp_task.smp_resp)-4);
	} else { /* DIRECT MODE */
		smp_cmd.long_smp_req.long_req_addr =
			cpu_to_le64((u64)sg_dma_address
					(&task->smp_task.smp_req));
		smp_cmd.long_smp_req.long_req_size =
			cpu_to_le32((u32)sg_dma_len(&task->smp_task.smp_req)-4);
		smp_cmd.long_smp_req.long_resp_addr =
			cpu_to_le64((u64)sg_dma_address
				(&task->smp_task.smp_resp));
		smp_cmd.long_smp_req.long_resp_size =
			cpu_to_le32
			((u32)sg_dma_len(&task->smp_task.smp_resp)-4);
	}
	if (pm8001_ha->smp_exp_mode == SMP_DIRECT) {
		pm8001_dbg(pm8001_ha, IO, "SMP REQUEST DIRECT MODE\n");
		for (i = 0; i < length; i++)
			if (i < 16) {
				smp_cmd.smp_req16[i] = *(payload + i);
				pm8001_dbg(pm8001_ha, IO,
					   "Byte[%d]:%x (DMA data:%x)\n",
					   i, smp_cmd.smp_req16[i],
					   *(payload));
			} else {
				smp_cmd.smp_req[i] = *(payload + i);
				pm8001_dbg(pm8001_ha, IO,
					   "Byte[%d]:%x (DMA data:%x)\n",
					   i, smp_cmd.smp_req[i],
					   *(payload));
			}
	}
	kunmap_atomic(to);
	build_smp_cmd(pm8001_dev->device_id, smp_cmd.tag,
				&smp_cmd, pm8001_ha->smp_exp_mode, length);
	rc = pm8001_mpi_build_cmd(pm8001_ha, circularQ, opc, &smp_cmd,
			sizeof(smp_cmd), 0);
	if (rc)
		goto err_out_2;
	return 0;

err_out_2:
	dma_unmap_sg(pm8001_ha->dev, &ccb->task->smp_task.smp_resp, 1,
			DMA_FROM_DEVICE);
err_out:
	dma_unmap_sg(pm8001_ha->dev, &ccb->task->smp_task.smp_req, 1,
			DMA_TO_DEVICE);
	return rc;
}

static int check_enc_sas_cmd(struct sas_task *task)
{
	u8 cmd = task->ssp_task.cmd->cmnd[0];

	if (cmd == READ_10 || cmd == WRITE_10 || cmd == WRITE_VERIFY)
		return 1;
	else
		return 0;
}

static int check_enc_sat_cmd(struct sas_task *task)
{
	int ret = 0;
	switch (task->ata_task.fis.command) {
	case ATA_CMD_FPDMA_READ:
	case ATA_CMD_READ_EXT:
	case ATA_CMD_READ:
	case ATA_CMD_FPDMA_WRITE:
	case ATA_CMD_WRITE_EXT:
	case ATA_CMD_WRITE:
	case ATA_CMD_PIO_READ:
	case ATA_CMD_PIO_READ_EXT:
	case ATA_CMD_PIO_WRITE:
	case ATA_CMD_PIO_WRITE_EXT:
		ret = 1;
		break;
	default:
		ret = 0;
		break;
	}
	return ret;
}

/**
 * pm80xx_chip_ssp_io_req - send an SSP task to FW
 * @pm8001_ha: our hba card information.
 * @ccb: the ccb information this request used.
 */
static int pm80xx_chip_ssp_io_req(struct pm8001_hba_info *pm8001_ha,
	struct pm8001_ccb_info *ccb)
{
	struct sas_task *task = ccb->task;
	struct domain_device *dev = task->dev;
	struct pm8001_device *pm8001_dev = dev->lldd_dev;
	struct ssp_ini_io_start_req ssp_cmd;
	u32 tag = ccb->ccb_tag;
	int ret;
	u64 phys_addr, start_addr, end_addr;
	u32 end_addr_high, end_addr_low;
	struct inbound_queue_table *circularQ;
	u32 q_index, cpu_id;
	u32 opc = OPC_INB_SSPINIIOSTART;
	memset(&ssp_cmd, 0, sizeof(ssp_cmd));
	memcpy(ssp_cmd.ssp_iu.lun, task->ssp_task.LUN, 8);
	/* data address domain added for spcv; set to 0 by host,
	 * used internally by controller
	 * 0 for SAS 1.1 and SAS 2.0 compatible TLR
	 */
	ssp_cmd.dad_dir_m_tlr =
		cpu_to_le32(data_dir_flags[task->data_dir] << 8 | 0x0);
	ssp_cmd.data_len = cpu_to_le32(task->total_xfer_len);
	ssp_cmd.device_id = cpu_to_le32(pm8001_dev->device_id);
	ssp_cmd.tag = cpu_to_le32(tag);
	if (task->ssp_task.enable_first_burst)
		ssp_cmd.ssp_iu.efb_prio_attr |= 0x80;
	ssp_cmd.ssp_iu.efb_prio_attr |= (task->ssp_task.task_prio << 3);
	ssp_cmd.ssp_iu.efb_prio_attr |= (task->ssp_task.task_attr & 7);
	memcpy(ssp_cmd.ssp_iu.cdb, task->ssp_task.cmd->cmnd,
		       task->ssp_task.cmd->cmd_len);
	cpu_id = smp_processor_id();
	q_index = (u32) (cpu_id) % (pm8001_ha->max_q_num);
	circularQ = &pm8001_ha->inbnd_q_tbl[q_index];

	/* Check if encryption is set */
	if (pm8001_ha->chip->encrypt &&
		!(pm8001_ha->encrypt_info.status) && check_enc_sas_cmd(task)) {
		pm8001_dbg(pm8001_ha, IO,
			   "Encryption enabled.Sending Encrypt SAS command 0x%x\n",
			   task->ssp_task.cmd->cmnd[0]);
		opc = OPC_INB_SSP_INI_DIF_ENC_IO;
		/* enable encryption. 0 for SAS 1.1 and SAS 2.0 compatible TLR*/
		ssp_cmd.dad_dir_m_tlr =	cpu_to_le32
			((data_dir_flags[task->data_dir] << 8) | 0x20 | 0x0);

		/* fill in PRD (scatter/gather) table, if any */
		if (task->num_scatter > 1) {
			pm8001_chip_make_sg(task->scatter,
						ccb->n_elem, ccb->buf_prd);
			phys_addr = ccb->ccb_dma_handle;
			ssp_cmd.enc_addr_low =
				cpu_to_le32(lower_32_bits(phys_addr));
			ssp_cmd.enc_addr_high =
				cpu_to_le32(upper_32_bits(phys_addr));
			ssp_cmd.enc_esgl = cpu_to_le32(1<<31);
		} else if (task->num_scatter == 1) {
			u64 dma_addr = sg_dma_address(task->scatter);
			ssp_cmd.enc_addr_low =
				cpu_to_le32(lower_32_bits(dma_addr));
			ssp_cmd.enc_addr_high =
				cpu_to_le32(upper_32_bits(dma_addr));
			ssp_cmd.enc_len = cpu_to_le32(task->total_xfer_len);
			ssp_cmd.enc_esgl = 0;
			/* Check 4G Boundary */
			start_addr = cpu_to_le64(dma_addr);
			end_addr = (start_addr + ssp_cmd.enc_len) - 1;
			end_addr_low = cpu_to_le32(lower_32_bits(end_addr));
			end_addr_high = cpu_to_le32(upper_32_bits(end_addr));
			if (end_addr_high != ssp_cmd.enc_addr_high) {
				pm8001_dbg(pm8001_ha, FAIL,
					   "The sg list address start_addr=0x%016llx data_len=0x%x end_addr_high=0x%08x end_addr_low=0x%08x has crossed 4G boundary\n",
					   start_addr, ssp_cmd.enc_len,
					   end_addr_high, end_addr_low);
				pm8001_chip_make_sg(task->scatter, 1,
					ccb->buf_prd);
				phys_addr = ccb->ccb_dma_handle;
				ssp_cmd.enc_addr_low =
					cpu_to_le32(lower_32_bits(phys_addr));
				ssp_cmd.enc_addr_high =
					cpu_to_le32(upper_32_bits(phys_addr));
				ssp_cmd.enc_esgl = cpu_to_le32(1<<31);
			}
		} else if (task->num_scatter == 0) {
			ssp_cmd.enc_addr_low = 0;
			ssp_cmd.enc_addr_high = 0;
			ssp_cmd.enc_len = cpu_to_le32(task->total_xfer_len);
			ssp_cmd.enc_esgl = 0;
		}
		/* XTS mode. All other fields are 0 */
		ssp_cmd.key_cmode = 0x6 << 4;
		/* set tweak values. Should be the start lba */
		ssp_cmd.twk_val0 = cpu_to_le32((task->ssp_task.cmd->cmnd[2] << 24) |
						(task->ssp_task.cmd->cmnd[3] << 16) |
						(task->ssp_task.cmd->cmnd[4] << 8) |
						(task->ssp_task.cmd->cmnd[5]));
	} else {
		pm8001_dbg(pm8001_ha, IO,
			   "Sending Normal SAS command 0x%x inb q %x\n",
			   task->ssp_task.cmd->cmnd[0], q_index);
		/* fill in PRD (scatter/gather) table, if any */
		if (task->num_scatter > 1) {
			pm8001_chip_make_sg(task->scatter, ccb->n_elem,
					ccb->buf_prd);
			phys_addr = ccb->ccb_dma_handle;
			ssp_cmd.addr_low =
				cpu_to_le32(lower_32_bits(phys_addr));
			ssp_cmd.addr_high =
				cpu_to_le32(upper_32_bits(phys_addr));
			ssp_cmd.esgl = cpu_to_le32(1<<31);
		} else if (task->num_scatter == 1) {
			u64 dma_addr = sg_dma_address(task->scatter);
			ssp_cmd.addr_low = cpu_to_le32(lower_32_bits(dma_addr));
			ssp_cmd.addr_high =
				cpu_to_le32(upper_32_bits(dma_addr));
			ssp_cmd.len = cpu_to_le32(task->total_xfer_len);
			ssp_cmd.esgl = 0;
			/* Check 4G Boundary */
			start_addr = cpu_to_le64(dma_addr);
			end_addr = (start_addr + ssp_cmd.len) - 1;
			end_addr_low = cpu_to_le32(lower_32_bits(end_addr));
			end_addr_high = cpu_to_le32(upper_32_bits(end_addr));
			if (end_addr_high != ssp_cmd.addr_high) {
				pm8001_dbg(pm8001_ha, FAIL,
					   "The sg list address start_addr=0x%016llx data_len=0x%x end_addr_high=0x%08x end_addr_low=0x%08x has crossed 4G boundary\n",
					   start_addr, ssp_cmd.len,
					   end_addr_high, end_addr_low);
				pm8001_chip_make_sg(task->scatter, 1,
					ccb->buf_prd);
				phys_addr = ccb->ccb_dma_handle;
				ssp_cmd.addr_low =
					cpu_to_le32(lower_32_bits(phys_addr));
				ssp_cmd.addr_high =
					cpu_to_le32(upper_32_bits(phys_addr));
				ssp_cmd.esgl = cpu_to_le32(1<<31);
			}
		} else if (task->num_scatter == 0) {
			ssp_cmd.addr_low = 0;
			ssp_cmd.addr_high = 0;
			ssp_cmd.len = cpu_to_le32(task->total_xfer_len);
			ssp_cmd.esgl = 0;
		}
	}
	ret = pm8001_mpi_build_cmd(pm8001_ha, circularQ, opc,
			&ssp_cmd, sizeof(ssp_cmd), q_index);
	return ret;
}

static int pm80xx_chip_sata_req(struct pm8001_hba_info *pm8001_ha,
	struct pm8001_ccb_info *ccb)
{
	struct sas_task *task = ccb->task;
	struct domain_device *dev = task->dev;
	struct pm8001_device *pm8001_ha_dev = dev->lldd_dev;
	struct ata_queued_cmd *qc = task->uldd_task;
	u32 tag = ccb->ccb_tag;
	int ret;
	u32 q_index, cpu_id;
	struct sata_start_req sata_cmd;
	u32 hdr_tag, ncg_tag = 0;
	u64 phys_addr, start_addr, end_addr;
	u32 end_addr_high, end_addr_low;
	u32 ATAP = 0x0;
	u32 dir;
	struct inbound_queue_table *circularQ;
	unsigned long flags;
	u32 opc = OPC_INB_SATA_HOST_OPSTART;
	memset(&sata_cmd, 0, sizeof(sata_cmd));
	cpu_id = smp_processor_id();
	q_index = (u32) (cpu_id) % (pm8001_ha->max_q_num);
	circularQ = &pm8001_ha->inbnd_q_tbl[q_index];

	if (task->data_dir == DMA_NONE) {
		ATAP = 0x04; /* no data*/
		pm8001_dbg(pm8001_ha, IO, "no data\n");
	} else if (likely(!task->ata_task.device_control_reg_update)) {
		if (task->ata_task.dma_xfer) {
			ATAP = 0x06; /* DMA */
			pm8001_dbg(pm8001_ha, IO, "DMA\n");
		} else {
			ATAP = 0x05; /* PIO*/
			pm8001_dbg(pm8001_ha, IO, "PIO\n");
		}
		if (task->ata_task.use_ncq &&
		    dev->sata_dev.class != ATA_DEV_ATAPI) {
			ATAP = 0x07; /* FPDMA */
			pm8001_dbg(pm8001_ha, IO, "FPDMA\n");
		}
	}
	if (task->ata_task.use_ncq && pm8001_get_ncq_tag(task, &hdr_tag)) {
		task->ata_task.fis.sector_count |= (u8) (hdr_tag << 3);
		ncg_tag = hdr_tag;
	}
	dir = data_dir_flags[task->data_dir] << 8;
	sata_cmd.tag = cpu_to_le32(tag);
	sata_cmd.device_id = cpu_to_le32(pm8001_ha_dev->device_id);
	sata_cmd.data_len = cpu_to_le32(task->total_xfer_len);

	sata_cmd.sata_fis = task->ata_task.fis;
	if (likely(!task->ata_task.device_control_reg_update))
		sata_cmd.sata_fis.flags |= 0x80;/* C=1: update ATA cmd reg */
	sata_cmd.sata_fis.flags &= 0xF0;/* PM_PORT field shall be 0 */

	/* Check if encryption is set */
	if (pm8001_ha->chip->encrypt &&
		!(pm8001_ha->encrypt_info.status) && check_enc_sat_cmd(task)) {
		pm8001_dbg(pm8001_ha, IO,
			   "Encryption enabled.Sending Encrypt SATA cmd 0x%x\n",
			   sata_cmd.sata_fis.command);
		opc = OPC_INB_SATA_DIF_ENC_IO;

		/* set encryption bit */
		sata_cmd.ncqtag_atap_dir_m_dad =
			cpu_to_le32(((ncg_tag & 0xff)<<16)|
				((ATAP & 0x3f) << 10) | 0x20 | dir);
							/* dad (bit 0-1) is 0 */
		/* fill in PRD (scatter/gather) table, if any */
		if (task->num_scatter > 1) {
			pm8001_chip_make_sg(task->scatter,
						ccb->n_elem, ccb->buf_prd);
			phys_addr = ccb->ccb_dma_handle;
			sata_cmd.enc_addr_low = lower_32_bits(phys_addr);
			sata_cmd.enc_addr_high = upper_32_bits(phys_addr);
			sata_cmd.enc_esgl = cpu_to_le32(1 << 31);
		} else if (task->num_scatter == 1) {
			u64 dma_addr = sg_dma_address(task->scatter);
			sata_cmd.enc_addr_low = lower_32_bits(dma_addr);
			sata_cmd.enc_addr_high = upper_32_bits(dma_addr);
			sata_cmd.enc_len = cpu_to_le32(task->total_xfer_len);
			sata_cmd.enc_esgl = 0;
			/* Check 4G Boundary */
			start_addr = cpu_to_le64(dma_addr);
			end_addr = (start_addr + sata_cmd.enc_len) - 1;
			end_addr_low = cpu_to_le32(lower_32_bits(end_addr));
			end_addr_high = cpu_to_le32(upper_32_bits(end_addr));
			if (end_addr_high != sata_cmd.enc_addr_high) {
				pm8001_dbg(pm8001_ha, FAIL,
					   "The sg list address start_addr=0x%016llx data_len=0x%x end_addr_high=0x%08x end_addr_low=0x%08x has crossed 4G boundary\n",
					   start_addr, sata_cmd.enc_len,
					   end_addr_high, end_addr_low);
				pm8001_chip_make_sg(task->scatter, 1,
					ccb->buf_prd);
				phys_addr = ccb->ccb_dma_handle;
				sata_cmd.enc_addr_low =
					lower_32_bits(phys_addr);
				sata_cmd.enc_addr_high =
					upper_32_bits(phys_addr);
				sata_cmd.enc_esgl =
					cpu_to_le32(1 << 31);
			}
		} else if (task->num_scatter == 0) {
			sata_cmd.enc_addr_low = 0;
			sata_cmd.enc_addr_high = 0;
			sata_cmd.enc_len = cpu_to_le32(task->total_xfer_len);
			sata_cmd.enc_esgl = 0;
		}
		/* XTS mode. All other fields are 0 */
		sata_cmd.key_index_mode = 0x6 << 4;
		/* set tweak values. Should be the start lba */
		sata_cmd.twk_val0 =
			cpu_to_le32((sata_cmd.sata_fis.lbal_exp << 24) |
					(sata_cmd.sata_fis.lbah << 16) |
					(sata_cmd.sata_fis.lbam << 8) |
					(sata_cmd.sata_fis.lbal));
		sata_cmd.twk_val1 =
			cpu_to_le32((sata_cmd.sata_fis.lbah_exp << 8) |
					 (sata_cmd.sata_fis.lbam_exp));
	} else {
		pm8001_dbg(pm8001_ha, IO,
			   "Sending Normal SATA command 0x%x inb %x\n",
			   sata_cmd.sata_fis.command, q_index);
		/* dad (bit 0-1) is 0 */
		sata_cmd.ncqtag_atap_dir_m_dad =
			cpu_to_le32(((ncg_tag & 0xff)<<16) |
					((ATAP & 0x3f) << 10) | dir);

		/* fill in PRD (scatter/gather) table, if any */
		if (task->num_scatter > 1) {
			pm8001_chip_make_sg(task->scatter,
					ccb->n_elem, ccb->buf_prd);
			phys_addr = ccb->ccb_dma_handle;
			sata_cmd.addr_low = lower_32_bits(phys_addr);
			sata_cmd.addr_high = upper_32_bits(phys_addr);
			sata_cmd.esgl = cpu_to_le32(1 << 31);
		} else if (task->num_scatter == 1) {
			u64 dma_addr = sg_dma_address(task->scatter);
			sata_cmd.addr_low = lower_32_bits(dma_addr);
			sata_cmd.addr_high = upper_32_bits(dma_addr);
			sata_cmd.len = cpu_to_le32(task->total_xfer_len);
			sata_cmd.esgl = 0;
			/* Check 4G Boundary */
			start_addr = cpu_to_le64(dma_addr);
			end_addr = (start_addr + sata_cmd.len) - 1;
			end_addr_low = cpu_to_le32(lower_32_bits(end_addr));
			end_addr_high = cpu_to_le32(upper_32_bits(end_addr));
			if (end_addr_high != sata_cmd.addr_high) {
				pm8001_dbg(pm8001_ha, FAIL,
					   "The sg list address start_addr=0x%016llx data_len=0x%xend_addr_high=0x%08x end_addr_low=0x%08x has crossed 4G boundary\n",
					   start_addr, sata_cmd.len,
					   end_addr_high, end_addr_low);
				pm8001_chip_make_sg(task->scatter, 1,
					ccb->buf_prd);
				phys_addr = ccb->ccb_dma_handle;
				sata_cmd.addr_low =
					lower_32_bits(phys_addr);
				sata_cmd.addr_high =
					upper_32_bits(phys_addr);
				sata_cmd.esgl = cpu_to_le32(1 << 31);
			}
		} else if (task->num_scatter == 0) {
			sata_cmd.addr_low = 0;
			sata_cmd.addr_high = 0;
			sata_cmd.len = cpu_to_le32(task->total_xfer_len);
			sata_cmd.esgl = 0;
		}
		/* scsi cdb */
		sata_cmd.atapi_scsi_cdb[0] =
			cpu_to_le32(((task->ata_task.atapi_packet[0]) |
			(task->ata_task.atapi_packet[1] << 8) |
			(task->ata_task.atapi_packet[2] << 16) |
			(task->ata_task.atapi_packet[3] << 24)));
		sata_cmd.atapi_scsi_cdb[1] =
			cpu_to_le32(((task->ata_task.atapi_packet[4]) |
			(task->ata_task.atapi_packet[5] << 8) |
			(task->ata_task.atapi_packet[6] << 16) |
			(task->ata_task.atapi_packet[7] << 24)));
		sata_cmd.atapi_scsi_cdb[2] =
			cpu_to_le32(((task->ata_task.atapi_packet[8]) |
			(task->ata_task.atapi_packet[9] << 8) |
			(task->ata_task.atapi_packet[10] << 16) |
			(task->ata_task.atapi_packet[11] << 24)));
		sata_cmd.atapi_scsi_cdb[3] =
			cpu_to_le32(((task->ata_task.atapi_packet[12]) |
			(task->ata_task.atapi_packet[13] << 8) |
			(task->ata_task.atapi_packet[14] << 16) |
			(task->ata_task.atapi_packet[15] << 24)));
	}

	/* Check for read log for failed drive and return */
	if (sata_cmd.sata_fis.command == 0x2f) {
		if (pm8001_ha_dev && ((pm8001_ha_dev->id & NCQ_READ_LOG_FLAG) ||
			(pm8001_ha_dev->id & NCQ_ABORT_ALL_FLAG) ||
			(pm8001_ha_dev->id & NCQ_2ND_RLE_FLAG))) {
			struct task_status_struct *ts;

			pm8001_ha_dev->id &= 0xDFFFFFFF;
			ts = &task->task_status;

			spin_lock_irqsave(&task->task_state_lock, flags);
			ts->resp = SAS_TASK_COMPLETE;
			ts->stat = SAS_SAM_STAT_GOOD;
			task->task_state_flags &= ~SAS_TASK_STATE_PENDING;
			task->task_state_flags &= ~SAS_TASK_AT_INITIATOR;
			task->task_state_flags |= SAS_TASK_STATE_DONE;
			if (unlikely((task->task_state_flags &
					SAS_TASK_STATE_ABORTED))) {
				spin_unlock_irqrestore(&task->task_state_lock,
							flags);
				pm8001_dbg(pm8001_ha, FAIL,
					   "task 0x%p resp 0x%x  stat 0x%x but aborted by upper layer\n",
					   task, ts->resp,
					   ts->stat);
				pm8001_ccb_task_free(pm8001_ha, task, ccb, tag);
				return 0;
			} else {
				spin_unlock_irqrestore(&task->task_state_lock,
							flags);
				pm8001_ccb_task_free_done(pm8001_ha, task,
								ccb, tag);
				atomic_dec(&pm8001_ha_dev->running_req);
				return 0;
			}
		}
	}
	trace_pm80xx_request_issue(pm8001_ha->id,
				ccb->device ? ccb->device->attached_phy : PM8001_MAX_PHYS,
				ccb->ccb_tag, opc,
				qc ? qc->tf.command : 0, // ata opcode
				ccb->device ? atomic_read(&ccb->device->running_req) : 0);
	ret = pm8001_mpi_build_cmd(pm8001_ha, circularQ, opc,
			&sata_cmd, sizeof(sata_cmd), q_index);
	return ret;
}

/**
 * pm80xx_chip_phy_start_req - start phy via PHY_START COMMAND
 * @pm8001_ha: our hba card information.
 * @phy_id: the phy id which we wanted to start up.
 */
static int
pm80xx_chip_phy_start_req(struct pm8001_hba_info *pm8001_ha, u8 phy_id)
{
	struct phy_start_req payload;
	struct inbound_queue_table *circularQ;
	int ret;
	u32 tag = 0x01;
	u32 opcode = OPC_INB_PHYSTART;
	circularQ = &pm8001_ha->inbnd_q_tbl[0];
	memset(&payload, 0, sizeof(payload));
	payload.tag = cpu_to_le32(tag);

	pm8001_dbg(pm8001_ha, INIT, "PHY START REQ for phy_id %d\n", phy_id);

	payload.ase_sh_lm_slr_phyid = cpu_to_le32(SPINHOLD_DISABLE |
			LINKMODE_AUTO | pm8001_ha->link_rate | phy_id);
	/* SSC Disable and SAS Analog ST configuration */
	/*
	payload.ase_sh_lm_slr_phyid =
		cpu_to_le32(SSC_DISABLE_30 | SAS_ASE | SPINHOLD_DISABLE |
		LINKMODE_AUTO | LINKRATE_15 | LINKRATE_30 | LINKRATE_60 |
		phy_id);
	Have to add "SAS PHY Analog Setup SPASTI 1 Byte" Based on need
	*/

	payload.sas_identify.dev_type = SAS_END_DEVICE;
	payload.sas_identify.initiator_bits = SAS_PROTOCOL_ALL;
	memcpy(payload.sas_identify.sas_addr,
	  &pm8001_ha->sas_addr, SAS_ADDR_SIZE);
	payload.sas_identify.phy_id = phy_id;
	ret = pm8001_mpi_build_cmd(pm8001_ha, circularQ, opcode, &payload,
			sizeof(payload), 0);
	return ret;
}

/**
 * pm80xx_chip_phy_stop_req - start phy via PHY_STOP COMMAND
 * @pm8001_ha: our hba card information.
 * @phy_id: the phy id which we wanted to start up.
 */
static int pm80xx_chip_phy_stop_req(struct pm8001_hba_info *pm8001_ha,
	u8 phy_id)
{
	struct phy_stop_req payload;
	struct inbound_queue_table *circularQ;
	int ret;
	u32 tag = 0x01;
	u32 opcode = OPC_INB_PHYSTOP;
	circularQ = &pm8001_ha->inbnd_q_tbl[0];
	memset(&payload, 0, sizeof(payload));
	payload.tag = cpu_to_le32(tag);
	payload.phy_id = cpu_to_le32(phy_id);
	ret = pm8001_mpi_build_cmd(pm8001_ha, circularQ, opcode, &payload,
			sizeof(payload), 0);
	return ret;
}

/*
 * see comments on pm8001_mpi_reg_resp.
 */
static int pm80xx_chip_reg_dev_req(struct pm8001_hba_info *pm8001_ha,
	struct pm8001_device *pm8001_dev, u32 flag)
{
	struct reg_dev_req payload;
	u32	opc;
	u32 stp_sspsmp_sata = 0x4;
	struct inbound_queue_table *circularQ;
	u32 linkrate, phy_id;
	int rc, tag = 0xdeadbeef;
	struct pm8001_ccb_info *ccb;
	u8 retryFlag = 0x1;
	u16 firstBurstSize = 0;
	u16 ITNT = 2000;
	struct domain_device *dev = pm8001_dev->sas_device;
	struct domain_device *parent_dev = dev->parent;
	struct pm8001_port *port = dev->port->lldd_port;
	circularQ = &pm8001_ha->inbnd_q_tbl[0];

	memset(&payload, 0, sizeof(payload));
	rc = pm8001_tag_alloc(pm8001_ha, &tag);
	if (rc)
		return rc;
	ccb = &pm8001_ha->ccb_info[tag];
	ccb->device = pm8001_dev;
	ccb->ccb_tag = tag;
	payload.tag = cpu_to_le32(tag);

	if (flag == 1) {
		stp_sspsmp_sata = 0x02; /*direct attached sata */
	} else {
		if (pm8001_dev->dev_type == SAS_SATA_DEV)
			stp_sspsmp_sata = 0x00; /* stp*/
		else if (pm8001_dev->dev_type == SAS_END_DEVICE ||
			dev_is_expander(pm8001_dev->dev_type))
			stp_sspsmp_sata = 0x01; /*ssp or smp*/
	}
	if (parent_dev && dev_is_expander(parent_dev->dev_type))
		phy_id = parent_dev->ex_dev.ex_phy->phy_id;
	else
		phy_id = pm8001_dev->attached_phy;

	opc = OPC_INB_REG_DEV;

	linkrate = (pm8001_dev->sas_device->linkrate < dev->port->linkrate) ?
			pm8001_dev->sas_device->linkrate : dev->port->linkrate;

	payload.phyid_portid =
		cpu_to_le32(((port->port_id) & 0xFF) |
		((phy_id & 0xFF) << 8));

	payload.dtype_dlr_mcn_ir_retry = cpu_to_le32((retryFlag & 0x01) |
		((linkrate & 0x0F) << 24) |
		((stp_sspsmp_sata & 0x03) << 28));
	payload.firstburstsize_ITNexustimeout =
		cpu_to_le32(ITNT | (firstBurstSize * 0x10000));

	memcpy(payload.sas_addr, pm8001_dev->sas_device->sas_addr,
		SAS_ADDR_SIZE);

	rc = pm8001_mpi_build_cmd(pm8001_ha, circularQ, opc, &payload,
			sizeof(payload), 0);
	if (rc)
		pm8001_tag_free(pm8001_ha, tag);

	return rc;
}

/**
 * pm80xx_chip_phy_ctl_req - support the local phy operation
 * @pm8001_ha: our hba card information.
 * @phyId: the phy id which we wanted to operate
 * @phy_op: phy operation to request
 */
static int pm80xx_chip_phy_ctl_req(struct pm8001_hba_info *pm8001_ha,
	u32 phyId, u32 phy_op)
{
	u32 tag;
	int rc;
	struct local_phy_ctl_req payload;
	struct inbound_queue_table *circularQ;
	u32 opc = OPC_INB_LOCAL_PHY_CONTROL;
	memset(&payload, 0, sizeof(payload));
	rc = pm8001_tag_alloc(pm8001_ha, &tag);
	if (rc)
		return rc;
	circularQ = &pm8001_ha->inbnd_q_tbl[0];
	payload.tag = cpu_to_le32(tag);
	payload.phyop_phyid =
		cpu_to_le32(((phy_op & 0xFF) << 8) | (phyId & 0xFF));
	return pm8001_mpi_build_cmd(pm8001_ha, circularQ, opc, &payload,
			sizeof(payload), 0);
}

static u32 pm80xx_chip_is_our_interrupt(struct pm8001_hba_info *pm8001_ha)
{
#ifdef PM8001_USE_MSIX
	return 1;
#else
	u32 value;

	value = pm8001_cr32(pm8001_ha, 0, MSGU_ODR);
	if (value)
		return 1;
	return 0;
#endif
}

/**
 * pm80xx_chip_isr - PM8001 isr handler.
 * @pm8001_ha: our hba card information.
 * @vec: irq number.
 */
static irqreturn_t
pm80xx_chip_isr(struct pm8001_hba_info *pm8001_ha, u8 vec)
{
	pm80xx_chip_interrupt_disable(pm8001_ha, vec);
	pm8001_dbg(pm8001_ha, DEVIO,
		   "irq vec %d, ODMR:0x%x\n",
		   vec, pm8001_cr32(pm8001_ha, 0, 0x30));
	process_oq(pm8001_ha, vec);
	pm80xx_chip_interrupt_enable(pm8001_ha, vec);
	return IRQ_HANDLED;
}

static void mpi_set_phy_profile_req(struct pm8001_hba_info *pm8001_ha,
				    u32 operation, u32 phyid,
				    u32 length, u32 *buf)
{
	u32 tag, i, j = 0;
	int rc;
	struct set_phy_profile_req payload;
	struct inbound_queue_table *circularQ;
	u32 opc = OPC_INB_SET_PHY_PROFILE;

	memset(&payload, 0, sizeof(payload));
	rc = pm8001_tag_alloc(pm8001_ha, &tag);
	if (rc)
		pm8001_dbg(pm8001_ha, FAIL, "Invalid tag\n");
	circularQ = &pm8001_ha->inbnd_q_tbl[0];
	payload.tag = cpu_to_le32(tag);
	payload.ppc_phyid = (((operation & 0xF) << 8) | (phyid  & 0xFF));
	pm8001_dbg(pm8001_ha, INIT,
		   " phy profile command for phy %x ,length is %d\n",
		   payload.ppc_phyid, length);
	for (i = length; i < (length + PHY_DWORD_LENGTH - 1); i++) {
		payload.reserved[j] =  cpu_to_le32(*((u32 *)buf + i));
		j++;
	}
	rc = pm8001_mpi_build_cmd(pm8001_ha, circularQ, opc, &payload,
			sizeof(payload), 0);
	if (rc)
		pm8001_tag_free(pm8001_ha, tag);
}

void pm8001_set_phy_profile(struct pm8001_hba_info *pm8001_ha,
	u32 length, u8 *buf)
{
	u32 i;

	for (i = 0; i < pm8001_ha->chip->n_phy; i++) {
		mpi_set_phy_profile_req(pm8001_ha,
			SAS_PHY_ANALOG_SETTINGS_PAGE, i, length, (u32 *)buf);
		length = length + PHY_DWORD_LENGTH;
	}
	pm8001_dbg(pm8001_ha, INIT, "phy settings completed\n");
}

void pm8001_set_phy_profile_single(struct pm8001_hba_info *pm8001_ha,
		u32 phy, u32 length, u32 *buf)
{
	u32 tag, opc;
	int rc, i;
	struct set_phy_profile_req payload;
	struct inbound_queue_table *circularQ;

	memset(&payload, 0, sizeof(payload));

	rc = pm8001_tag_alloc(pm8001_ha, &tag);
	if (rc)
		pm8001_dbg(pm8001_ha, INIT, "Invalid tag\n");

	circularQ = &pm8001_ha->inbnd_q_tbl[0];
	opc = OPC_INB_SET_PHY_PROFILE;

	payload.tag = cpu_to_le32(tag);
	payload.ppc_phyid = (((SAS_PHY_ANALOG_SETTINGS_PAGE & 0xF) << 8)
				| (phy & 0xFF));

	for (i = 0; i < length; i++)
		payload.reserved[i] = cpu_to_le32(*(buf + i));

	rc = pm8001_mpi_build_cmd(pm8001_ha, circularQ, opc, &payload,
			sizeof(payload), 0);
	if (rc)
		pm8001_tag_free(pm8001_ha, tag);

	pm8001_dbg(pm8001_ha, INIT, "PHY %d settings applied\n", phy);
}
const struct pm8001_dispatch pm8001_80xx_dispatch = {
	.name			= "pmc80xx",
	.chip_init		= pm80xx_chip_init,
	.chip_soft_rst		= pm80xx_chip_soft_rst,
	.chip_rst		= pm80xx_hw_chip_rst,
	.chip_iounmap		= pm8001_chip_iounmap,
	.isr			= pm80xx_chip_isr,
	.is_our_interrupt	= pm80xx_chip_is_our_interrupt,
	.isr_process_oq		= process_oq,
	.interrupt_enable	= pm80xx_chip_interrupt_enable,
	.interrupt_disable	= pm80xx_chip_interrupt_disable,
	.make_prd		= pm8001_chip_make_sg,
	.smp_req		= pm80xx_chip_smp_req,
	.ssp_io_req		= pm80xx_chip_ssp_io_req,
	.sata_req		= pm80xx_chip_sata_req,
	.phy_start_req		= pm80xx_chip_phy_start_req,
	.phy_stop_req		= pm80xx_chip_phy_stop_req,
	.reg_dev_req		= pm80xx_chip_reg_dev_req,
	.dereg_dev_req		= pm8001_chip_dereg_dev_req,
	.phy_ctl_req		= pm80xx_chip_phy_ctl_req,
	.task_abort		= pm8001_chip_abort_task,
	.ssp_tm_req		= pm8001_chip_ssp_tm_req,
	.get_nvmd_req		= pm8001_chip_get_nvmd_req,
	.set_nvmd_req		= pm8001_chip_set_nvmd_req,
	.fw_flash_update_req	= pm8001_chip_fw_flash_update_req,
	.set_dev_state_req	= pm8001_chip_set_dev_state_req,
	.fatal_errors		= pm80xx_fatal_errors,
	.hw_event_ack_req	= pm80xx_hw_event_ack_req,
};
