#ifndef pata_rdc_H
#define pata_rdc_H

#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

/* ATA Configuration Register ID offset address size */
#define ATAConfiguration_PCIOffset			0x40
#define ATAConfiguration_ID_PrimaryTiming		0x00
#define ATAConfiguration_ID_SecondaryTiming		0x02
#define ATAConfiguration_ID_Device1Timing		0x04
#define ATAConfiguration_ID_UDMAControl			0x08
#define ATAConfiguration_ID_UDMATiming			0x0A
#define ATAConfiguration_ID_IDEIOConfiguration		0x14

#define ATAConfiguration_ID_PrimaryTiming_Size		2
#define ATAConfiguration_ID_SecondaryTiming_Size	2
#define ATAConfiguration_ID_Device1Timing_Size		1
#define ATAConfiguration_ID_UDMAControl_Size		1
#define ATAConfiguration_ID_UDMATiming_Size		2
#define ATAConfiguration_ID_IDEIOConfiguration_Size	4

/* ATA Configuration Register bit define */
#define ATAConfiguration_PrimaryTiming_Device0FastTimingEnable		0x0001
#define ATAConfiguration_PrimaryTiming_Device0IORDYSampleModeEnable	0x0002	/* PIO 3 or greater */
#define ATAConfiguration_PrimaryTiming_Device0PrefetchandPostingEnable	0x0004	/* PIO 2 or greater */
#define ATAConfiguration_PrimaryTiming_Device0DMATimingEnable		0x0008
#define ATAConfiguration_PrimaryTiming_Device1FastTimingEnable		0x0010
#define ATAConfiguration_PrimaryTiming_Device1IORDYSampleModeEnable	0x0020	/* PIO 3 or greater */
#define ATAConfiguration_PrimaryTiming_Device1PrefetchandPostingEnable	0x0040	/* PIO 2 or greater */
#define ATAConfiguration_PrimaryTiming_Device1DMATimingEnable		0x0080
#define ATAConfiguration_PrimaryTiming_Device0RecoveryMode		0x0300
#define ATAConfiguration_PrimaryTiming_Device0RecoveryMode_0		0x0000	/* PIO 0, PIO 2, MDMA 0 */
#define ATAConfiguration_PrimaryTiming_Device0RecoveryMode_1		0x0100	/* PIO 3, MDMA 1 */
#define ATAConfiguration_PrimaryTiming_Device0RecoveryMode_2		0x0200	/* X */
#define ATAConfiguration_PrimaryTiming_Device0RecoveryMode_3		0x0300	/* PIO 4, MDMA 2 */
#define ATAConfiguration_PrimaryTiming_Device0IORDYSampleMode		0x3000
#define ATAConfiguration_PrimaryTiming_Device0IORDYSampleMode_0		0x0000	/* PIO 0 */
#define ATAConfiguration_PrimaryTiming_Device0IORDYSampleMode_1		0x1000	/* PIO 2, MDMA 0 */
#define ATAConfiguration_PrimaryTiming_Device0IORDYSampleMode_2		0x2000	/* PIO 3, PIO 4, MDMA 1, MDMA 2 */
#define ATAConfiguration_PrimaryTiming_Device0IORDYSampleMode_3		0x3000	/* X */
#define ATAConfiguration_PrimaryTiming_Device1TimingRegisterEnable	0x4000
#define ATAConfiguration_PrimaryTiming_IDEDecodeEnable			0x8000

#define ATAConfiguration_Device1Timing_PrimaryRecoveryMode		0x0003
#define ATAConfiguration_Device1Timing_PrimaryRecoveryMode_0		0x0000
#define ATAConfiguration_Device1Timing_PrimaryRecoveryMode_1		0x0001
#define ATAConfiguration_Device1Timing_PrimaryRecoveryMode_2		0x0002
#define ATAConfiguration_Device1Timing_PrimaryRecoveryMode_3		0x0003
#define ATAConfiguration_Device1Timing_PrimaryIORDYSampleMode		0x000C
#define ATAConfiguration_Device1Timing_PrimaryIORDYSampleMode_0		0x0000
#define ATAConfiguration_Device1Timing_PrimaryIORDYSampleMode_1		0x0004
#define ATAConfiguration_Device1Timing_PrimaryIORDYSampleMode_2		0x0008
#define ATAConfiguration_Device1Timing_PrimaryIORDYSampleMode_3		0x000C
#define ATAConfiguration_Device1Timing_SecondaryRecoveryMode		0x0030
#define ATAConfiguration_Device1Timing_SecondaryRecoveryMode_0		0x0000
#define ATAConfiguration_Device1Timing_SecondaryRecoveryMode_1		0x0010
#define ATAConfiguration_Device1Timing_SecondaryRecoveryMode_2		0x0020
#define ATAConfiguration_Device1Timing_SecondaryRecoveryMode_3		0x0030
#define ATAConfiguration_Device1Timing_SecondaryIORDYSampleMode		0x00C0
#define ATAConfiguration_Device1Timing_SecondaryIORDYSampleMode_0	0x0000
#define ATAConfiguration_Device1Timing_SecondaryIORDYSampleMode_1	0x0040
#define ATAConfiguration_Device1Timing_SecondaryIORDYSampleMode_2	0x0080
#define ATAConfiguration_Device1Timing_SecondaryIORDYSampleMode_3	0x00C0

#define ATAConfiguration_UDMAControl_PrimaryDevice0UDMAModeEnable	0x0001
#define ATAConfiguration_UDMAControl_PrimaryDevice1UDMAModeEnable	0x0002
#define ATAConfiguration_UDMAControl_SecondaryDevice0UDMAModeEnable	0x0004
#define ATAConfiguration_UDMAControl_SecondaryDevice1UDMAModeEnable	0x0008

#define ATAConfiguration_UDMATiming_PrimaryDevice0CycleTime		0x0003
#define ATAConfiguration_UDMATiming_PrimaryDevice0CycleTime_0		0x0000	/* UDMA 0 */
#define ATAConfiguration_UDMATiming_PrimaryDevice0CycleTime_1		0x0001	/* UDMA 1, UDMA 3, UDMA 5 */
#define ATAConfiguration_UDMATiming_PrimaryDevice0CycleTime_2		0x0002	/* UDMA 2, UDMA 4 */
#define ATAConfiguration_UDMATiming_PrimaryDevice0CycleTime_3		0x0003	/* X */
#define ATAConfiguration_UDMATiming_PrimaryDevice1CycleTime		0x0030
#define ATAConfiguration_UDMATiming_PrimaryDevice1CycleTime_0		0x0000	/* UDMA 0 */
#define ATAConfiguration_UDMATiming_PrimaryDevice1CycleTime_1		0x0010	/* UDMA 1, UDMA 3, UDMA 5 */
#define ATAConfiguration_UDMATiming_PrimaryDevice1CycleTime_2		0x0020	/* UDMA 2, UDMA 4 */
#define ATAConfiguration_UDMATiming_PrimaryDevice1CycleTime_3		0x0030	/* X */
#define ATAConfiguration_UDMATiming_SecondaryDevice0CycleTime		0x0300
#define ATAConfiguration_UDMATiming_SecondaryDevice0CycleTime_0		0x0000	/* UDMA 0 */
#define ATAConfiguration_UDMATiming_SecondaryDevice0CycleTime_1		0x0100	/* UDMA 1, UDMA 3, UDMA 5 */
#define ATAConfiguration_UDMATiming_SecondaryDevice0CycleTime_2		0x0200	/* UDMA 2, UDMA 4 */
#define ATAConfiguration_UDMATiming_SecondaryDevice0CycleTime_3		0x0300	/* X */
#define ATAConfiguration_UDMATiming_SecondaryDevice1CycleTime		0x3000
#define ATAConfiguration_UDMATiming_SecondaryDevice1CycleTime_0		0x0000	/* UDMA 0 */
#define ATAConfiguration_UDMATiming_SecondaryDevice1CycleTime_1		0x1000	/* UDMA 1, UDMA 3, UDMA 5 */
#define ATAConfiguration_UDMATiming_SecondaryDevice1CycleTime_2		0x2000	/* UDMA 2, UDMA 4 */
#define ATAConfiguration_UDMATiming_SecondaryDevice1CycleTime_3		0x3000	/* X */

#define ATAConfiguration_IDEIOConfiguration_PrimaryDevice066MhzEnable		0x00000001	/* UDMA 3, UDMA 4 */
#define ATAConfiguration_IDEIOConfiguration_PrimaryDevice166MhzEnable		0x00000002
#define ATAConfiguration_IDEIOConfiguration_SecondaryDevice066MhzEnable		0x00000004
#define ATAConfiguration_IDEIOConfiguration_SecondaryDevice166MhzEnable		0x00000008
#define ATAConfiguration_IDEIOConfiguration_DeviceCable80Report			0x000000F0
#define ATAConfiguration_IDEIOConfiguration_PrimaryDeviceCable80Report		0x00000030
#define ATAConfiguration_IDEIOConfiguration_PrimaryDevice0Cable80Report		0x00000010	/* UDMA 3, UDMA 4, UDMA 5 */
#define ATAConfiguration_IDEIOConfiguration_PrimaryDevice1Cable80Report		0x00000020
#define ATAConfiguration_IDEIOConfiguration_SecondaryDeviceCable80Report	0x000000C0
#define ATAConfiguration_IDEIOConfiguration_SecondaryDevice0Cable80Report	0x00000040
#define ATAConfiguration_IDEIOConfiguration_SecondaryDevice1Cable80Report	0x00000080
#define ATAConfiguration_IDEIOConfiguration_PrimaryDevice0100MhzEnable		0x00001000	/* UDMA 5 */
#define ATAConfiguration_IDEIOConfiguration_PrimaryDevice1100MhzEnable		0x00002000
#define ATAConfiguration_IDEIOConfiguration_SecondaryDevice0100MhzEnable	0x00004000
#define ATAConfiguration_IDEIOConfiguration_SecondaryDevice1100MhzEnable	0x00008000
#define ATAConfiguration_IDEIOConfiguration_ATA100IsSupported			0x00F00000

enum _PIOTimingMode {
	PIO0 = 0,
	PIO1,
	PIO2,	/* MDMA 0 */
	PIO3,	/* MDMA 1 */
	PIO4	/* MDMA 2 */
};

enum _DMATimingMode {
	MDMA0 = 0,
	MDMA1,
	MDMA2
};

enum _UDMATimingMode {
	UDMA0 = 0,
	UDMA1,
	UDMA2,
	UDMA3,
	UDMA4,
	UDMA5
};


enum rdc_controller_ids {
	/* controller IDs */
	RDC_17F31011,
	RDC_17F31012
};

/* callback function for driver */
static int rdc_init_one(struct pci_dev *pdev, const struct pci_device_id *ent);

/* callback function for ata_port */
static int rdc_pata_port_start(struct ata_port *ap);

static void rdc_pata_port_stop(struct ata_port *ap);

static int rdc_pata_prereset(struct ata_link *link, unsigned long deadline);

static int rdc_pata_cable_detect(struct ata_port *ap);

static void rdc_pata_set_piomode(struct ata_port *ap, struct ata_device *adev);

static void rdc_pata_set_dmamode(struct ata_port *ap, struct ata_device *adev);

/* modified PCIDeviceIO code. */
static uint PCIDeviceIO_ReadPCIConfiguration(struct pci_dev *pdev, uint Offset, uint Length, void *pBuffer);

static uint PCIDeviceIO_WritePCIConfiguration(struct pci_dev *pdev, uint Offset, uint Length, void *pBuffer);

/* modify ATAHostAdapter code */
static uint ATAHostAdapter_SetPrimaryPIO(struct pci_dev *pdev, uint DeviceID, uint PIOTimingMode, uint DMAEnable, uint PrefetchPostingEnable);

static uint ATAHostAdapter_SetSecondaryPIO(struct pci_dev *pdev, uint DeviceID, uint PIOTimingMode, uint DMAEnable, uint PrefetchPostingEnable);

static uint ATAHostAdapter_SetPrimaryUDMA(struct pci_dev *pdev, uint DeviceID, uint UDMAEnable, uint UDMATimingMode);

static uint ATAHostAdapter_SetSecondaryUDMA(struct pci_dev *pdev, uint DeviceID, uint UDMAEnable, uint UDMATimingMode);

#endif
