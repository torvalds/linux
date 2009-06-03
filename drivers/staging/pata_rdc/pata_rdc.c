#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

#include <linux/pci.h>
#include <linux/device.h>

#include <scsi/scsi_host.h>
#include <linux/libata.h>

#include "pata_rdc.h"

/* #define DBGPRINTF */

#ifdef DBGPRINTF

    #define dbgprintf(format, arg...) printk(KERN_INFO format, ## arg)

#else

    #define dbgprintf(...)

#endif

/* Driver Info. */
#define DRIVER_NAME         "pata_rdc"	/* sata_rdc for SATA */
#define DRIVER_VERSION      "2.6.28"    /* based on kernel version. */
					/* because each kernel main version has
					 * its libata, we follow kernel to
					 * determine the last libata version.
					 */


static const struct pci_device_id rdc_pata_id_table[] = {
	{ PCI_DEVICE(0x17F3, 0x1011), RDC_17F31011},
	{ PCI_DEVICE(0x17F3, 0x1012), RDC_17F31012},
	{ }	/* terminate list */
};
MODULE_DEVICE_TABLE(pci, rdc_pata_id_table);

static unsigned int in_module_init = 1; /* hotplugging check??? */

/* ata device data */

/* see ATA Host Adapters Standards. */
static struct pci_bits ATA_Decode_Enable_Bits[] = {
	{ 0x41U, 1U, 0x80UL, 0x80UL },	/* port (Channel) 0 */
	{ 0x43U, 1U, 0x80UL, 0x80UL },	/* port (Channel) 1 */
};

static uint PCIDeviceIO_ReadPCIConfiguration(struct pci_dev *pdev, uint Offset, uint Length, void *pBuffer)
{
	uint funcresult;
	unchar *pchar;
	uint i;

	funcresult = TRUE;

	pchar = pBuffer;

	for (i = 0; i < Length; i++) {
		pci_read_config_byte(pdev, Offset, pchar);
		Offset++;
		pchar++;
	}

	funcresult = TRUE;

	goto funcexit;
funcexit:

	return funcresult;
}

static uint PCIDeviceIO_WritePCIConfiguration(struct pci_dev *pdev, uint Offset, uint Length, void *pBuffer)
{
	uint funcresult;
	unchar *pchar;
	uint i;

	funcresult = TRUE;

	pchar = pBuffer;

	for (i = 0; i < Length; i++) {
		pci_write_config_byte(pdev, Offset, *pchar);
		Offset++;
		pchar++;
	}

	funcresult = TRUE;

	goto funcexit;
funcexit:

	return funcresult;
}

static uint ATAHostAdapter_SetPrimaryPIO(struct pci_dev *pdev, uint DeviceID,
					 uint PIOTimingMode, uint DMAEnable,
					 uint PrefetchPostingEnable)
{
	uint funcresult;
	uint result;
	uint ATATimingRegister;
	uint Device1TimingRegister;

	funcresult = TRUE;

	ATATimingRegister = 0;
	Device1TimingRegister = 0;

	result = PCIDeviceIO_ReadPCIConfiguration(pdev,
						 ATAConfiguration_ID_PrimaryTiming + ATAConfiguration_PCIOffset,
						 ATAConfiguration_ID_PrimaryTiming_Size,
						 &ATATimingRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	result = PCIDeviceIO_ReadPCIConfiguration(pdev,
						  ATAConfiguration_ID_Device1Timing + ATAConfiguration_PCIOffset,
						  ATAConfiguration_ID_Device1Timing_Size,
						  &Device1TimingRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device1TimingRegisterEnable;

	switch (DeviceID) {
	case 0:
		/* mask clear */
		ATATimingRegister &= ~(ATAConfiguration_PrimaryTiming_Device0FastTimingEnable |
				      ATAConfiguration_PrimaryTiming_Device0IORDYSampleModeEnable |
				      ATAConfiguration_PrimaryTiming_Device0PrefetchandPostingEnable |
				      ATAConfiguration_PrimaryTiming_Device0DMATimingEnable |
				      ATAConfiguration_PrimaryTiming_Device0RecoveryMode |
				      ATAConfiguration_PrimaryTiming_Device0IORDYSampleMode);

		if (PIOTimingMode > PIO0)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0FastTimingEnable;

		if (PIOTimingMode >= PIO3)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0IORDYSampleModeEnable;

		if (PIOTimingMode >= PIO2 && PrefetchPostingEnable == TRUE)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0PrefetchandPostingEnable;

		if (DMAEnable == TRUE && PIOTimingMode >= PIO2)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0DMATimingEnable;

		if (PIOTimingMode <= PIO2)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0RecoveryMode_0;
		else if (PIOTimingMode == PIO3)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0RecoveryMode_1;
		else if (PIOTimingMode == PIO4)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0RecoveryMode_3;

		if (PIOTimingMode <= PIO1)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0IORDYSampleMode_0;
		else if (PIOTimingMode == PIO2)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0IORDYSampleMode_1;
		else if (PIOTimingMode <= PIO4)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0IORDYSampleMode_2;
		break;
	case 1:
		ATATimingRegister &= ~(ATAConfiguration_PrimaryTiming_Device1FastTimingEnable |
				       ATAConfiguration_PrimaryTiming_Device1IORDYSampleModeEnable |
				       ATAConfiguration_PrimaryTiming_Device1PrefetchandPostingEnable |
				       ATAConfiguration_PrimaryTiming_Device1DMATimingEnable);

		if (PIOTimingMode > PIO0)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device1FastTimingEnable;

		if (PIOTimingMode >= PIO3)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device1IORDYSampleModeEnable;

		if (PIOTimingMode >= PIO2 && PrefetchPostingEnable == TRUE)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device1PrefetchandPostingEnable;

		if (DMAEnable == TRUE && PIOTimingMode >= PIO2)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device1DMATimingEnable;

		Device1TimingRegister &= ~(ATAConfiguration_Device1Timing_PrimaryRecoveryMode |
					   ATAConfiguration_Device1Timing_PrimaryIORDYSampleMode);

		if (PIOTimingMode <= PIO2)
			Device1TimingRegister |= ATAConfiguration_Device1Timing_PrimaryRecoveryMode_0;
		else if (PIOTimingMode == PIO3)
			Device1TimingRegister |= ATAConfiguration_Device1Timing_PrimaryRecoveryMode_1;
		else if (PIOTimingMode == PIO4)
			Device1TimingRegister |= ATAConfiguration_Device1Timing_PrimaryRecoveryMode_3;

		if (PIOTimingMode <= PIO1)
			Device1TimingRegister |= ATAConfiguration_Device1Timing_PrimaryIORDYSampleMode_0;
		else if (PIOTimingMode == PIO2)
			Device1TimingRegister |= ATAConfiguration_Device1Timing_PrimaryIORDYSampleMode_1;
		else if (PIOTimingMode <= PIO4)
			Device1TimingRegister |= ATAConfiguration_Device1Timing_PrimaryIORDYSampleMode_2;
		break;
	default:
		funcresult = FALSE;
		goto funcexit;
		break;
	}

	result = PCIDeviceIO_WritePCIConfiguration(pdev,
						   ATAConfiguration_ID_PrimaryTiming + ATAConfiguration_PCIOffset,
						   ATAConfiguration_ID_PrimaryTiming_Size,
						   &ATATimingRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	result = PCIDeviceIO_WritePCIConfiguration(pdev,
						   ATAConfiguration_ID_Device1Timing + ATAConfiguration_PCIOffset,
						   ATAConfiguration_ID_Device1Timing_Size,
						   &Device1TimingRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	goto funcexit;
funcexit:

	return funcresult;
}

static uint ATAHostAdapter_SetSecondaryPIO(struct pci_dev *pdev, uint DeviceID,
					   uint PIOTimingMode, uint DMAEnable,
					   uint PrefetchPostingEnable)
{
	uint funcresult;
	uint result;
	uint ATATimingRegister;
	uint Device1TimingRegister;

	funcresult = TRUE;

	ATATimingRegister = 0;
	Device1TimingRegister = 0;

	result = PCIDeviceIO_ReadPCIConfiguration(pdev,
						  ATAConfiguration_ID_SecondaryTiming + ATAConfiguration_PCIOffset,
						  ATAConfiguration_ID_SecondaryTiming_Size,
						  &ATATimingRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	result = PCIDeviceIO_ReadPCIConfiguration(pdev,
						  ATAConfiguration_ID_Device1Timing + ATAConfiguration_PCIOffset,
						  ATAConfiguration_ID_Device1Timing_Size,
						  &Device1TimingRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device1TimingRegisterEnable;

	switch (DeviceID) {
	case 0:
		/* mask clear */
		ATATimingRegister &= ~(ATAConfiguration_PrimaryTiming_Device0FastTimingEnable |
				       ATAConfiguration_PrimaryTiming_Device0IORDYSampleModeEnable |
				       ATAConfiguration_PrimaryTiming_Device0PrefetchandPostingEnable |
				       ATAConfiguration_PrimaryTiming_Device0DMATimingEnable |
				       ATAConfiguration_PrimaryTiming_Device0RecoveryMode |
				       ATAConfiguration_PrimaryTiming_Device0IORDYSampleMode);

		if (PIOTimingMode > PIO0)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0FastTimingEnable;

		if (PIOTimingMode >= PIO3)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0IORDYSampleModeEnable;

		if (PIOTimingMode >= PIO2 && PrefetchPostingEnable == TRUE)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0PrefetchandPostingEnable;

		if (DMAEnable == TRUE && PIOTimingMode >= PIO2)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0DMATimingEnable;

		if (PIOTimingMode <= PIO2)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0RecoveryMode_0;
		else if (PIOTimingMode == PIO3)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0RecoveryMode_1;
		else if (PIOTimingMode == PIO4)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0RecoveryMode_3;

		if (PIOTimingMode <= PIO1)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0IORDYSampleMode_0;
		else if (PIOTimingMode == PIO2)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0IORDYSampleMode_1;
		else if (PIOTimingMode <= PIO4)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device0IORDYSampleMode_2;
		break;
	case 1:
		ATATimingRegister &= ~(ATAConfiguration_PrimaryTiming_Device1FastTimingEnable |
				       ATAConfiguration_PrimaryTiming_Device1IORDYSampleModeEnable |
				       ATAConfiguration_PrimaryTiming_Device1PrefetchandPostingEnable |
				       ATAConfiguration_PrimaryTiming_Device1DMATimingEnable);

		if (PIOTimingMode > PIO0)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device1FastTimingEnable;

		if (PIOTimingMode >= PIO3)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device1IORDYSampleModeEnable;

		if (PIOTimingMode >= PIO2 && PrefetchPostingEnable == TRUE)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device1PrefetchandPostingEnable;

		if (DMAEnable == TRUE && PIOTimingMode >= PIO2)
			ATATimingRegister |= ATAConfiguration_PrimaryTiming_Device1DMATimingEnable;

		Device1TimingRegister &= ~(ATAConfiguration_Device1Timing_SecondaryRecoveryMode |
					   ATAConfiguration_Device1Timing_SecondaryIORDYSampleMode);

		if (PIOTimingMode <= PIO2)
			Device1TimingRegister |= ATAConfiguration_Device1Timing_SecondaryRecoveryMode_0;
		else if (PIOTimingMode == PIO3)
			Device1TimingRegister |= ATAConfiguration_Device1Timing_SecondaryRecoveryMode_1;
		else if (PIOTimingMode == PIO4)
			Device1TimingRegister |= ATAConfiguration_Device1Timing_SecondaryRecoveryMode_3;

		if (PIOTimingMode <= PIO1)
			Device1TimingRegister |= ATAConfiguration_Device1Timing_SecondaryIORDYSampleMode_0;
		else if (PIOTimingMode == PIO2)
			Device1TimingRegister |= ATAConfiguration_Device1Timing_SecondaryIORDYSampleMode_1;
		else if (PIOTimingMode <= PIO4)
			Device1TimingRegister |= ATAConfiguration_Device1Timing_SecondaryIORDYSampleMode_2;
		break;
	default:
		funcresult = FALSE;
		goto funcexit;
		break;
	}

	result = PCIDeviceIO_WritePCIConfiguration(pdev,
						   ATAConfiguration_ID_SecondaryTiming + ATAConfiguration_PCIOffset,
						   ATAConfiguration_ID_SecondaryTiming_Size,
						   &ATATimingRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	result = PCIDeviceIO_WritePCIConfiguration(pdev,
						   ATAConfiguration_ID_Device1Timing + ATAConfiguration_PCIOffset,
						   ATAConfiguration_ID_Device1Timing_Size,
						   &Device1TimingRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	goto funcexit;
funcexit:
	return funcresult;
}

static uint ATAHostAdapter_SetPrimaryUDMA(struct pci_dev *pdev, uint DeviceID,
					  uint UDMAEnable, uint UDMATimingMode)
{
	uint funcresult;
	uint result;
	uint UDMAControlRegister;
	uint UDMATimingRegister;
	ulong IDEIOConfigurationRegister;

	funcresult = TRUE;
	UDMAControlRegister = 0;
	UDMATimingRegister = 0;
	IDEIOConfigurationRegister = 0;

	result = PCIDeviceIO_ReadPCIConfiguration(pdev,
						  ATAConfiguration_ID_UDMAControl + ATAConfiguration_PCIOffset,
						  ATAConfiguration_ID_UDMAControl_Size,
						  &UDMAControlRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	result = PCIDeviceIO_ReadPCIConfiguration(pdev,
						  ATAConfiguration_ID_UDMATiming + ATAConfiguration_PCIOffset,
						  ATAConfiguration_ID_UDMATiming_Size,
						  &UDMATimingRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	result = PCIDeviceIO_ReadPCIConfiguration(pdev,
						  ATAConfiguration_ID_IDEIOConfiguration + ATAConfiguration_PCIOffset,
						  ATAConfiguration_ID_IDEIOConfiguration_Size,
						  &IDEIOConfigurationRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	/*Rom Code will determine the device cable type and ATA 100.*/
	/*IDEIOConfigurationRegister |= ATAConfiguration_IDEIOConfiguration_DeviceCable80Report;*/
	/*IDEIOConfigurationRegister |= ATAConfiguration_IDEIOConfiguration_ATA100IsSupported;*/

	switch (DeviceID) {
	case 0:
		UDMAControlRegister &= ~(ATAConfiguration_UDMAControl_PrimaryDevice0UDMAModeEnable);
		if (UDMAEnable == TRUE)
			UDMAControlRegister |= ATAConfiguration_UDMAControl_PrimaryDevice0UDMAModeEnable;

		IDEIOConfigurationRegister &= ~(ATAConfiguration_IDEIOConfiguration_PrimaryDevice066MhzEnable |
						ATAConfiguration_IDEIOConfiguration_PrimaryDevice0100MhzEnable);

		if (UDMATimingMode >= UDMA5)
			IDEIOConfigurationRegister |= ATAConfiguration_IDEIOConfiguration_PrimaryDevice0100MhzEnable;
		else if (UDMATimingMode >= UDMA3)
			IDEIOConfigurationRegister |= ATAConfiguration_IDEIOConfiguration_PrimaryDevice066MhzEnable;

		/* if 80 cable report */
		UDMATimingRegister &= ~(ATAConfiguration_UDMATiming_PrimaryDevice0CycleTime);

		if (UDMATimingMode == UDMA0) {
			UDMATimingRegister |= ATAConfiguration_UDMATiming_PrimaryDevice0CycleTime_0;
		} else if (UDMATimingMode == UDMA1 ||
			   UDMATimingMode == UDMA3 ||
			   UDMATimingMode == UDMA5) {
			UDMATimingRegister |= ATAConfiguration_UDMATiming_PrimaryDevice0CycleTime_1;
		} else if (UDMATimingMode == UDMA2 ||
			   UDMATimingMode == UDMA4) {
			UDMATimingRegister |= ATAConfiguration_UDMATiming_PrimaryDevice0CycleTime_2;
		}
		break;
	case 1:
		UDMAControlRegister &= ~(ATAConfiguration_UDMAControl_PrimaryDevice1UDMAModeEnable);
		if (UDMAEnable == TRUE)
			UDMAControlRegister |= ATAConfiguration_UDMAControl_PrimaryDevice1UDMAModeEnable;

		IDEIOConfigurationRegister &= ~(ATAConfiguration_IDEIOConfiguration_PrimaryDevice166MhzEnable |
						ATAConfiguration_IDEIOConfiguration_PrimaryDevice1100MhzEnable);

		if (UDMATimingMode >= UDMA5)
			IDEIOConfigurationRegister |= ATAConfiguration_IDEIOConfiguration_PrimaryDevice1100MhzEnable;
		else if (UDMATimingMode >= UDMA3)
			IDEIOConfigurationRegister |= ATAConfiguration_IDEIOConfiguration_PrimaryDevice166MhzEnable;

		/* if 80 cable report */
		UDMATimingRegister &= ~(ATAConfiguration_UDMATiming_PrimaryDevice1CycleTime);

		if (UDMATimingMode == UDMA0) {
			UDMATimingRegister |= ATAConfiguration_UDMATiming_PrimaryDevice1CycleTime_0;
		} else if (UDMATimingMode == UDMA1 ||
			   UDMATimingMode == UDMA3 ||
			   UDMATimingMode == UDMA5) {
			UDMATimingRegister |= ATAConfiguration_UDMATiming_PrimaryDevice1CycleTime_1;
		} else if (UDMATimingMode == UDMA2 ||
			   UDMATimingMode == UDMA4) {
			UDMATimingRegister |= ATAConfiguration_UDMATiming_PrimaryDevice1CycleTime_2;
		}
		break;
	default:
		funcresult = FALSE;
		goto funcexit;
		break;
	}

	result = PCIDeviceIO_WritePCIConfiguration(pdev,
						   ATAConfiguration_ID_UDMAControl + ATAConfiguration_PCIOffset,
						   ATAConfiguration_ID_UDMAControl_Size,
						   &UDMAControlRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	result = PCIDeviceIO_WritePCIConfiguration(pdev,
						   ATAConfiguration_ID_UDMATiming + ATAConfiguration_PCIOffset,
						   ATAConfiguration_ID_UDMATiming_Size,
						   &UDMATimingRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	result = PCIDeviceIO_WritePCIConfiguration(pdev,
						   ATAConfiguration_ID_IDEIOConfiguration + ATAConfiguration_PCIOffset,
						   ATAConfiguration_ID_IDEIOConfiguration_Size,
						   &IDEIOConfigurationRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	goto funcexit;
funcexit:
	return funcresult;
}

static uint ATAHostAdapter_SetSecondaryUDMA(struct pci_dev *pdev, uint DeviceID,
					    uint UDMAEnable, uint UDMATimingMode)
{
	uint funcresult;
	uint result;
	uint UDMAControlRegister;
	uint UDMATimingRegister;
	ulong IDEIOConfigurationRegister;

	funcresult = TRUE;

	UDMAControlRegister = 0;
	UDMATimingRegister = 0;
	IDEIOConfigurationRegister = 0;

	result = PCIDeviceIO_ReadPCIConfiguration(pdev,
						  ATAConfiguration_ID_UDMAControl + ATAConfiguration_PCIOffset,
						  ATAConfiguration_ID_UDMAControl_Size,
						  &UDMAControlRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	result = PCIDeviceIO_ReadPCIConfiguration(pdev,
						  ATAConfiguration_ID_UDMATiming + ATAConfiguration_PCIOffset,
						  ATAConfiguration_ID_UDMATiming_Size,
						  &UDMATimingRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	result = PCIDeviceIO_ReadPCIConfiguration(pdev,
						  ATAConfiguration_ID_IDEIOConfiguration + ATAConfiguration_PCIOffset,
						  ATAConfiguration_ID_IDEIOConfiguration_Size,
						  &IDEIOConfigurationRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	/* Rom Code will determine the device cable type and ATA 100. */
	/* IDEIOConfigurationRegister |= ATAConfiguration_IDEIOConfiguration_DeviceCable80Report; */
	/* IDEIOConfigurationRegister |= ATAConfiguration_IDEIOConfiguration_ATA100IsSupported; */

	switch (DeviceID) {
	case 0:
		UDMAControlRegister &= ~(ATAConfiguration_UDMAControl_SecondaryDevice0UDMAModeEnable);
		if (UDMAEnable == TRUE)
			UDMAControlRegister |= ATAConfiguration_UDMAControl_SecondaryDevice0UDMAModeEnable;

		IDEIOConfigurationRegister &= ~(ATAConfiguration_IDEIOConfiguration_SecondaryDevice066MhzEnable |
						ATAConfiguration_IDEIOConfiguration_SecondaryDevice0100MhzEnable);

		if (UDMATimingMode >= UDMA5)
			IDEIOConfigurationRegister |= ATAConfiguration_IDEIOConfiguration_SecondaryDevice0100MhzEnable;
		else if (UDMATimingMode >= UDMA3)
			IDEIOConfigurationRegister |= ATAConfiguration_IDEIOConfiguration_SecondaryDevice066MhzEnable;

		/* if 80 cable report */
		UDMATimingRegister &= ~(ATAConfiguration_UDMATiming_SecondaryDevice0CycleTime);

		if (UDMATimingMode == UDMA0) {
			UDMATimingRegister |= ATAConfiguration_UDMATiming_SecondaryDevice0CycleTime_0;
		} else if (UDMATimingMode == UDMA1 ||
			   UDMATimingMode == UDMA3 ||
			   UDMATimingMode == UDMA5) {
			UDMATimingRegister |= ATAConfiguration_UDMATiming_SecondaryDevice0CycleTime_1;
		} else if (UDMATimingMode == UDMA2 ||
			   UDMATimingMode == UDMA4) {
			UDMATimingRegister |= ATAConfiguration_UDMATiming_SecondaryDevice0CycleTime_2;
		}
		break;
	case 1:
		UDMAControlRegister &= ~(ATAConfiguration_UDMAControl_SecondaryDevice1UDMAModeEnable);
		if (UDMAEnable == TRUE)
			UDMAControlRegister |= ATAConfiguration_UDMAControl_SecondaryDevice1UDMAModeEnable;

		IDEIOConfigurationRegister &= ~(ATAConfiguration_IDEIOConfiguration_SecondaryDevice166MhzEnable |
						ATAConfiguration_IDEIOConfiguration_SecondaryDevice1100MhzEnable);

		if (UDMATimingMode >= UDMA5)
			IDEIOConfigurationRegister |= ATAConfiguration_IDEIOConfiguration_SecondaryDevice1100MhzEnable;
		else if (UDMATimingMode >= UDMA3)
			IDEIOConfigurationRegister |= ATAConfiguration_IDEIOConfiguration_SecondaryDevice166MhzEnable;

		/* if 80 cable report */
		UDMATimingRegister &= ~(ATAConfiguration_UDMATiming_SecondaryDevice1CycleTime);

		if (UDMATimingMode == UDMA0) {
			UDMATimingRegister |= ATAConfiguration_UDMATiming_SecondaryDevice1CycleTime_0;
		} else if (UDMATimingMode == UDMA1 ||
			   UDMATimingMode == UDMA3 ||
			   UDMATimingMode == UDMA5) {
			UDMATimingRegister |= ATAConfiguration_UDMATiming_SecondaryDevice1CycleTime_1;
		} else if (UDMATimingMode == UDMA2 ||
			   UDMATimingMode == UDMA4) {
			UDMATimingRegister |= ATAConfiguration_UDMATiming_SecondaryDevice1CycleTime_2;
		}
		break;
	default:
		funcresult = FALSE;
		goto funcexit;
		break;
	}

	result = PCIDeviceIO_WritePCIConfiguration(pdev,
						   ATAConfiguration_ID_UDMAControl + ATAConfiguration_PCIOffset,
						   ATAConfiguration_ID_UDMAControl_Size,
						   &UDMAControlRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	result = PCIDeviceIO_WritePCIConfiguration(pdev,
						   ATAConfiguration_ID_UDMATiming + ATAConfiguration_PCIOffset,
						   ATAConfiguration_ID_UDMATiming_Size,
						   &UDMATimingRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	result = PCIDeviceIO_WritePCIConfiguration(pdev,
						   ATAConfiguration_ID_IDEIOConfiguration + ATAConfiguration_PCIOffset,
						   ATAConfiguration_ID_IDEIOConfiguration_Size,
						   &IDEIOConfigurationRegister);
	if (result == FALSE) {
		funcresult = FALSE;
		goto funcexit;
	}

	goto funcexit;
funcexit:
	return funcresult;
}

/**
 *    Set port up for dma.
 *    @ap: Port to initialize
 *
 *    Called just after data structures for each port are
 *    initialized.  Allocates space for PRD table if the device
 *    is DMA capable SFF.

    Some drivers also use this entry point as a chance to allocate driverprivate
    memory for ap->private_data.

 *
 *    May be used as the port_start() entry in ata_port_operations.
 *
 *    LOCKING:
 *    Inherited from caller.
 */
static int rdc_pata_port_start(struct ata_port *ap)
{
	uint    Channel;

	Channel = ap->port_no;
	dbgprintf("rdc_pata_port_start Channel: %u \n", Channel);
	if (ap->ioaddr.bmdma_addr) {
		return ata_port_start(ap);
	} else {
		dbgprintf("rdc_pata_port_start return 0 !!!\n");
		return 0;
	}
}

static void rdc_pata_port_stop(struct ata_port *ap)
{
	uint    Channel;

	Channel = ap->port_no;

	dbgprintf("rdc_pata_port_stop Channel: %u \n", Channel);
}

/**
 *    prereset for PATA host controller
 *    @link: Target link
 *    @deadline: deadline jiffies for the operation
 *
 *    LOCKING:
 *    None (inherited from caller).
 */
static int rdc_pata_prereset(struct ata_link *link, unsigned long deadline)
{
	struct pci_dev *pdev;
	struct ata_port *ap;
	uint Channel;

	dbgprintf("rdc_pata_prereset\n");

	ap = link->ap;
	pdev = to_pci_dev(ap->host->dev);

	Channel = ap->port_no;

	/* test ATA Decode Enable Bits, should be enable. */
	if (!pci_test_config_bits(pdev, &ATA_Decode_Enable_Bits[Channel])) {
		dbgprintf("rdc_pata_prereset Channel: %u, Decode Disable\n", Channel);
		return -ENOENT;
	} else {
		dbgprintf("rdc_pata_prereset Channel: %u, Decode Enable\n", Channel);
		return ata_std_prereset(link, deadline);
	}
}

/**
 *    Probe host controller cable detect info
 *    @ap: Port for which cable detect info is desired
 *
 *    Read cable indicator from ATA PCI device's PCI config
 *    register.  This register is normally set by firmware (BIOS).
 *
 *    LOCKING:
 *    None (inherited from caller).
 */
static int rdc_pata_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev;
	uint Channel;
	uint Mask;
	u32 u32Value;

	dbgprintf("rdc_pata_cable_detect\n");

	pdev = to_pci_dev(ap->host->dev);

	Channel = ap->port_no;

	if (Channel == 0)
		Mask = ATAConfiguration_IDEIOConfiguration_PrimaryDeviceCable80Report;
	else
		Mask = ATAConfiguration_IDEIOConfiguration_SecondaryDeviceCable80Report;

	/* check BIOS cable detect results */
	pci_read_config_dword(pdev, ATAConfiguration_ID_IDEIOConfiguration + ATAConfiguration_PCIOffset, &u32Value);

	if ((u32Value & Mask) == 0) {
		dbgprintf("rdc_pata_cable_detect Channel: %u, PATA40 \n", Channel);
		return ATA_CBL_PATA40;
	} else {
		dbgprintf("rdc_pata_cable_detect Channel: %u, PATA80 \n", Channel);
		return ATA_CBL_PATA80;
	}
}

/**
 *    Initialize host controller PATA PIO timings
 *    @ap: Port whose timings we are configuring
 *    @adev: um
 *
 *    Set PIO mode for device, in host controller PCI config space.
 *
 *    LOCKING:
 *    None (inherited from caller).
 */
static void rdc_pata_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev;
	uint    Channel;
	uint    DeviceID;
	uint    PIOTimingMode;
	uint    PrefetchPostingEnable;

	dbgprintf("rdc_pata_set_piomode\n");

	pdev = to_pci_dev(ap->host->dev);

	Channel = ap->port_no;
	DeviceID = adev->devno;
	/*
	 * piomode = 0, 1, 2, 3... ; adev->pio_mode = XFER_PIO_0, XFER_PIO_1,
	 * XFER_PIO_2, XFER_PIO_3...
	 */
	PIOTimingMode = adev->pio_mode - XFER_PIO_0;

	if (adev->class == ATA_DEV_ATA) {
		PrefetchPostingEnable = TRUE;
	} else {
		/* ATAPI, CD DVD Rom */
		PrefetchPostingEnable = FALSE;
	}

	/* PIO configuration clears DTE unconditionally.  It will be
	 * programmed in set_dmamode which is guaranteed to be called
	 * after set_piomode if any DMA mode is available.
	 */

	/* Ensure the UDMA bit is off - it will be turned back on if UDMA is
	 * selected */

	if (Channel == 0) {
		ATAHostAdapter_SetPrimaryPIO(
		    pdev,
		    DeviceID,
		    PIOTimingMode,
		    TRUE,/* DMAEnable, */
		    PrefetchPostingEnable
		    );

		ATAHostAdapter_SetPrimaryUDMA(
		    pdev,
		    DeviceID,
		    FALSE,/* UDMAEnable, */
		    UDMA0
		    );
	} else {
		ATAHostAdapter_SetSecondaryPIO(
		    pdev,
		    DeviceID,
		    PIOTimingMode,
		    TRUE,/* DMAEnable, */
		    PrefetchPostingEnable
		    );

		ATAHostAdapter_SetSecondaryUDMA(
		    pdev,
		    DeviceID,
		    FALSE,/* UDMAEnable, */
		    UDMA0
		    );
	}
	dbgprintf("rdc_pata_set_piomode Channel: %u, DeviceID: %u, PIO: %d \n", Channel, DeviceID, PIOTimingMode);
}

/**
 *    Initialize host controller PATA DMA timings
 *    @ap: Port whose timings we are configuring
 *    @adev: um
 *
 *    Set MW/UDMA mode for device, in host controller PCI config space.
 *
 *    LOCKING:
 *    None (inherited from caller).
 */
static void rdc_pata_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev;
	uint    Channel;
	uint    DeviceID;
	uint    PIOTimingMode;
	uint    PrefetchPostingEnable;
	uint    DMATimingMode;
	uint    UDMAEnable;

	dbgprintf("rdc_pata_set_dmamode\n");

	pdev = to_pci_dev(ap->host->dev);

	Channel = ap->port_no;
	DeviceID = adev->devno;
	PIOTimingMode = adev->pio_mode - XFER_PIO_0;  /* piomode = 0, 1, 2, 3... ; adev->pio_mode = XFER_PIO_0, XFER_PIO_1, XFER_PIO_2, XFER_PIO_3... */
	DMATimingMode = adev->dma_mode; /* UDMA or MDMA */

	if (adev->class == ATA_DEV_ATA) {
		PrefetchPostingEnable = TRUE;
	} else {
		/* ATAPI, CD DVD Rom */
		PrefetchPostingEnable = FALSE;
	}

	if (ap->udma_mask == 0) {
		/* ata_port dont support udma. depend on hardware spec. */
		UDMAEnable = FALSE;
	} else {
		UDMAEnable = TRUE;
	}

	/*if (ap->mdma_mask == 0) {
	}*/

	if (Channel == 0) {
		if (DMATimingMode >= XFER_UDMA_0) {
			/* UDMA */
			ATAHostAdapter_SetPrimaryPIO(pdev,
				DeviceID,
				PIOTimingMode,
				TRUE,/*DMAEnable,*/
				PrefetchPostingEnable);

			ATAHostAdapter_SetPrimaryUDMA(pdev,
				DeviceID,
				UDMAEnable,
				DMATimingMode - XFER_UDMA_0);
			dbgprintf("rdc_pata_set_dmamode Channel: %u, DeviceID: %u, UDMA: %u \n", Channel, DeviceID, (uint)(DMATimingMode - XFER_UDMA_0));
		} else {
			/* MDMA */
			ATAHostAdapter_SetPrimaryPIO(pdev,
				DeviceID,
				(DMATimingMode - XFER_MW_DMA_0) + PIO2, /* MDMA0 = PIO2 */
				TRUE,/*DMAEnable,*/
				PrefetchPostingEnable);

			ATAHostAdapter_SetPrimaryUDMA(pdev,
				DeviceID,
				FALSE,/*UDMAEnable,*/
				UDMA0);
			dbgprintf("rdc_pata_set_dmamode Channel: %u, DeviceID: %u, MDMA: %u \n", Channel, DeviceID, (uint)(DMATimingMode - XFER_MW_DMA_0));
		}
	} else {
		if (DMATimingMode >= XFER_UDMA_0) {
			/* UDMA */
			ATAHostAdapter_SetSecondaryPIO(pdev,
				DeviceID,
				PIOTimingMode,
				TRUE,/*DMAEnable,*/
				PrefetchPostingEnable);

			ATAHostAdapter_SetSecondaryUDMA(pdev,
				DeviceID,
				UDMAEnable,
				DMATimingMode - XFER_UDMA_0);
			dbgprintf("rdc_pata_set_dmamode Channel: %u, DeviceID: %u, UDMA: %u \n", Channel, DeviceID, (uint)(DMATimingMode - XFER_UDMA_0));
		} else {
			/* MDMA */
			ATAHostAdapter_SetSecondaryPIO(pdev,
				DeviceID,
				(DMATimingMode - XFER_MW_DMA_0) + PIO2, /* MDMA0 = PIO2 */
				TRUE,/*DMAEnable,*/
				PrefetchPostingEnable);

			ATAHostAdapter_SetSecondaryUDMA(pdev,
				DeviceID,
				FALSE,/*UDMAEnable,*/
				UDMA0);
			dbgprintf("rdc_pata_set_dmamode Channel: %u, DeviceID: %u, MDMA: %u \n", Channel, DeviceID, (uint)(DMATimingMode - XFER_MW_DMA_0));
		}
	}
}

/* pata host template */
static struct scsi_host_template rdc_pata_sht = {
	ATA_BMDMA_SHT(DRIVER_NAME),
};

static struct ata_port_operations rdc_pata_ops = {
	.inherits	= &ata_bmdma_port_ops,

	.port_start	= rdc_pata_port_start,
	.port_stop	= rdc_pata_port_stop,
	.prereset	= rdc_pata_prereset,
	.cable_detect	= rdc_pata_cable_detect,
	.set_piomode	= rdc_pata_set_piomode,
	.set_dmamode	= rdc_pata_set_dmamode,
};

static struct ata_port_info rdc_pata_port_info[] = {
	[RDC_17F31011] = {
	.flags		= ATA_FLAG_SLAVE_POSS,
	.pio_mask	= 0x1f,		/* pio0-4 */
	.mwdma_mask	= 0x07,		/* mwdma0-2 */
	.udma_mask	= ATA_UDMA5,	/* udma0-5 */
	.port_ops	= &rdc_pata_ops,
	},

	[RDC_17F31012] = {
	.flags		= ATA_FLAG_SLAVE_POSS,
	.pio_mask	= 0x1f,		/* pio0-4 */
	.mwdma_mask	= 0x07,		/* mwdma0-2 */
	.udma_mask	= ATA_UDMA5,	/* udma0-5 */
	.port_ops	= &rdc_pata_ops,
	},
};

static int __devinit rdc_init_one(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	/*struct device *dev = &pdev->dev; */
	struct ata_port_info port_info[2];
	const struct ata_port_info *ppinfo[] = { &port_info[0], &port_info[1] };

	int rc;

	dbgprintf("rdc_init_one\n");

	/* no hotplugging support (FIXME) */ /* why??? */
	if (!in_module_init) {
		dbgprintf("rdc_init_one in_module_init == 0 failed \n");
		return -ENODEV;
	}
	port_info[0] = rdc_pata_port_info[ent->driver_data];
	port_info[1] = rdc_pata_port_info[ent->driver_data];

	/* enable device and prepare host */
	rc = pci_enable_device(pdev);
	if (rc) {
		dbgprintf("rdc_init_one pci_enable_device failed \n");
		return rc;
	}
	/* initialize controller */

	pci_intx(pdev, 1);  /* enable interrupt */

	return ata_pci_sff_init_one(pdev, ppinfo, &rdc_pata_sht, NULL);
}

/* a pci driver */
static struct pci_driver rdc_pata_driver = {
	.name		= DRIVER_NAME,
	.id_table	= rdc_pata_id_table,
	.probe		= rdc_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= ata_pci_device_resume,
#endif
};

static int __init pata_rdc_init(void)
{
	int rc;

	dbgprintf("pata_rdc_init\n");
	rc = pci_register_driver(&rdc_pata_driver);
	if (rc) {
		dbgprintf("pata_rdc_init faile\n");
		return rc;
	}

	in_module_init = 0;

	return 0;
}

static void __exit pata_rdc_exit(void)
{
	dbgprintf("pata_rdc_exit\n");
	pci_unregister_driver(&rdc_pata_driver);
}

module_init(pata_rdc_init);
module_exit(pata_rdc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RDC PCI IDE Driver");
MODULE_VERSION(DRIVER_VERSION);
