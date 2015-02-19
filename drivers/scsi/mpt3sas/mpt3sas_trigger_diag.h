/*
 * This is the Fusion MPT base driver providing common API layer interface
 * to set Diagnostic triggers for MPT (Message Passing Technology) based
 * controllers
 *
 * This code is based on drivers/scsi/mpt3sas/mpt3sas_base.h
 * Copyright (C) 2012-2014  LSI Corporation
 * Copyright (C) 2013-2014 Avago Technologies
 *  (mailto: MPT-FusionLinux.pdl@avagotech.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * NO WARRANTY
 * THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
 * LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
 * solely responsible for determining the appropriateness of using and
 * distributing the Program and assumes all risks associated with its
 * exercise of rights under this Agreement, including but not limited to
 * the risks and costs of program errors, damage to or loss of data,
 * programs or equipment, and unavailability or interruption of operations.

 * DISCLAIMER OF LIABILITY
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */
  /* Diagnostic Trigger Configuration Data Structures */

#ifndef MPT3SAS_TRIGGER_DIAG_H_INCLUDED
#define MPT3SAS_TRIGGER_DIAG_H_INCLUDED

/* limitation on number of entries */
#define NUM_VALID_ENTRIES               (20)

/* trigger types */
#define MPT3SAS_TRIGGER_MASTER          (1)
#define MPT3SAS_TRIGGER_EVENT           (2)
#define MPT3SAS_TRIGGER_SCSI            (3)
#define MPT3SAS_TRIGGER_MPI             (4)

/* trigger names */
#define MASTER_TRIGGER_FILE_NAME        "diag_trigger_master"
#define EVENT_TRIGGERS_FILE_NAME        "diag_trigger_event"
#define SCSI_TRIGGERS_FILE_NAME         "diag_trigger_scsi"
#define MPI_TRIGGER_FILE_NAME           "diag_trigger_mpi"

/* master trigger bitmask */
#define MASTER_TRIGGER_FW_FAULT         (0x00000001)
#define MASTER_TRIGGER_ADAPTER_RESET    (0x00000002)
#define MASTER_TRIGGER_TASK_MANAGMENT   (0x00000004)
#define MASTER_TRIGGER_DEVICE_REMOVAL   (0x00000008)

/* fake firmware event for tigger */
#define MPI3_EVENT_DIAGNOSTIC_TRIGGER_FIRED	(0x6E)

/**
 * MasterTrigger is a single U32 passed to/from sysfs.
 *
 * Bit Flags (enables) include:
 * 1. FW Faults
 * 2. Adapter Reset issued by driver
 * 3. TMs
 * 4. Device Remove Event sent by FW
 */

struct SL_WH_MASTER_TRIGGER_T {
	uint32_t MasterData;
};

/**
 * struct SL_WH_EVENT_TRIGGER_T -  Definition of an event trigger element
 * @EventValue: Event Code to trigger on
 * @LogEntryQualifier: Type of FW event that logged (Log Entry Added Event only)
 *
 * Defines an event that should induce a DIAG_TRIGGER driver event if observed.
 */
struct SL_WH_EVENT_TRIGGER_T {
	uint16_t EventValue;
	uint16_t LogEntryQualifier;
};

/**
 * struct SL_WH_EVENT_TRIGGERS_T -  Structure passed to/from sysfs containing a
 *    list of Event Triggers to be monitored for.
 * @ValidEntries: Number of _SL_WH_EVENT_TRIGGER_T structures contained in this
 *    structure.
 * @EventTriggerEntry: List of Event trigger elements.
 *
 * This binary structure is transferred via sysfs to get/set Event Triggers
 * in the Linux Driver.
 */

struct SL_WH_EVENT_TRIGGERS_T {
	uint32_t ValidEntries;
	struct SL_WH_EVENT_TRIGGER_T EventTriggerEntry[NUM_VALID_ENTRIES];
};

/**
 * struct SL_WH_SCSI_TRIGGER_T -  Definition of a SCSI trigger element
 * @ASCQ: Additional Sense Code Qualifier.  Can be specific or 0xFF for
 *     wildcard.
 * @ASC: Additional Sense Code.  Can be specific or 0xFF for wildcard
 * @SenseKey: SCSI Sense Key
 *
 * Defines a sense key (single or many variants) that should induce a
 * DIAG_TRIGGER driver event if observed.
 */
struct SL_WH_SCSI_TRIGGER_T {
	U8 ASCQ;
	U8 ASC;
	U8 SenseKey;
	U8 Reserved;
};

/**
 * struct SL_WH_SCSI_TRIGGERS_T -  Structure passed to/from sysfs containing a
 *    list of SCSI sense codes that should trigger a DIAG_SERVICE event when
 *    observed.
 * @ValidEntries: Number of _SL_WH_SCSI_TRIGGER_T structures contained in this
 *    structure.
 * @SCSITriggerEntry: List of SCSI Sense Code trigger elements.
 *
 * This binary structure is transferred via sysfs to get/set SCSI Sense Code
 * Triggers in the Linux Driver.
 */
struct SL_WH_SCSI_TRIGGERS_T {
	uint32_t ValidEntries;
	struct SL_WH_SCSI_TRIGGER_T SCSITriggerEntry[NUM_VALID_ENTRIES];
};

/**
 * struct SL_WH_MPI_TRIGGER_T -  Definition of an MPI trigger element
 * @IOCStatus: MPI IOCStatus
 * @IocLogInfo: MPI IocLogInfo.  Can be specific or 0xFFFFFFFF for wildcard
 *
 * Defines a MPI IOCStatus/IocLogInfo pair that should induce a DIAG_TRIGGER
 * driver event if observed.
 */
struct SL_WH_MPI_TRIGGER_T {
	uint16_t IOCStatus;
	uint16_t Reserved;
	uint32_t IocLogInfo;
};

/**
 * struct SL_WH_MPI_TRIGGERS_T -  Structure passed to/from sysfs containing a
 *    list of MPI IOCStatus/IocLogInfo pairs that should trigger a DIAG_SERVICE
 *    event when observed.
 * @ValidEntries: Number of _SL_WH_MPI_TRIGGER_T structures contained in this
 *    structure.
 * @MPITriggerEntry: List of MPI IOCStatus/IocLogInfo trigger elements.
 *
 * This binary structure is transferred via sysfs to get/set MPI Error Triggers
 * in the Linux Driver.
 */
struct SL_WH_MPI_TRIGGERS_T {
	uint32_t ValidEntries;
	struct SL_WH_MPI_TRIGGER_T MPITriggerEntry[NUM_VALID_ENTRIES];
};

/**
 * struct SL_WH_TRIGGERS_EVENT_DATA_T -  event data for trigger
 * @trigger_type: trigger type (see MPT3SAS_TRIGGER_XXXX)
 * @u: trigger condition that caused trigger to be sent
 */
struct SL_WH_TRIGGERS_EVENT_DATA_T {
	uint32_t trigger_type;
	union {
		struct SL_WH_MASTER_TRIGGER_T master;
		struct SL_WH_EVENT_TRIGGER_T event;
		struct SL_WH_SCSI_TRIGGER_T scsi;
		struct SL_WH_MPI_TRIGGER_T mpi;
	} u;
};
#endif /* MPT3SAS_TRIGGER_DIAG_H_INCLUDED */
