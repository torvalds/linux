#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include <asm/i387.h>

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#define APCI1710_SAVE_INTERRUPT	1

union str_ModuleInfo {
	/* Incremental counter infos */
	struct {
		union {
			struct {
				unsigned char b_ModeRegister1;
				unsigned char b_ModeRegister2;
				unsigned char b_ModeRegister3;
				unsigned char b_ModeRegister4;
			} s_ByteModeRegister;
			unsigned int dw_ModeRegister1_2_3_4;
		} s_ModeRegister;

		struct {
			unsigned int b_IndexInit:1;
			unsigned int b_CounterInit:1;
			unsigned int b_ReferenceInit:1;
			unsigned int b_IndexInterruptOccur:1;
			unsigned int b_CompareLogicInit:1;
			unsigned int b_FrequencyMeasurementInit:1;
			unsigned int b_FrequencyMeasurementEnable:1;
		} s_InitFlag;

	} s_SiemensCounterInfo;

	/* SSI infos */
	struct {
		unsigned char b_SSIProfile;
		unsigned char b_PositionTurnLength;
		unsigned char b_TurnCptLength;
		unsigned char b_SSIInit;
	} s_SSICounterInfo;

	/* TTL I/O infos */
	struct {
		unsigned char b_TTLInit;
		unsigned char b_PortConfiguration[4];
	} s_TTLIOInfo;

	/* Digital I/O infos */
	struct {
		unsigned char b_DigitalInit;
		unsigned char b_ChannelAMode;
		unsigned char b_ChannelBMode;
		unsigned char b_OutputMemoryEnabled;
		unsigned int dw_OutputMemory;
	} s_DigitalIOInfo;

	/* 82X54 timer infos */
	struct {
		struct {
			unsigned char b_82X54Init;
			unsigned char b_InputClockSelection;
			unsigned char b_InputClockLevel;
			unsigned char b_OutputLevel;
			unsigned char b_HardwareGateLevel;
			unsigned int dw_ConfigurationWord;
		} s_82X54TimerInfo[3];
		unsigned char b_InterruptMask;
	} s_82X54ModuleInfo;

	/* Chronometer infos */
	struct {
		unsigned char b_ChronoInit;
		unsigned char b_InterruptMask;
		unsigned char b_PCIInputClock;
		unsigned char b_TimingUnit;
		unsigned char b_CycleMode;
		double d_TimingInterval;
		unsigned int dw_ConfigReg;
	} s_ChronoModuleInfo;

	/* Pulse encoder infos */
	struct {
		struct {
			unsigned char b_PulseEncoderInit;
		} s_PulseEncoderInfo[4];
		unsigned int dw_SetRegister;
		unsigned int dw_ControlRegister;
		unsigned int dw_StatusRegister;
	} s_PulseEncoderModuleInfo;

	/* Tor conter infos */
	struct {
		struct {
			unsigned char b_TorCounterInit;
			unsigned char b_TimingUnit;
			unsigned char b_InterruptEnable;
			double d_TimingInterval;
			unsigned int ul_RealTimingInterval;
		} s_TorCounterInfo[2];
		unsigned char b_PCIInputClock;
	} s_TorCounterModuleInfo;

	/* PWM infos */
	struct {
		struct {
			unsigned char b_PWMInit;
			unsigned char b_TimingUnit;
			unsigned char b_InterruptEnable;
			double d_LowTiming;
			double d_HighTiming;
			unsigned int ul_RealLowTiming;
			unsigned int ul_RealHighTiming;
		} s_PWMInfo[2];
		unsigned char b_ClockSelection;
	} s_PWMModuleInfo;

	/* CDA infos */
	struct {
		unsigned char b_CDAEnable;
		unsigned char b_FctSelection;
	} s_CDAModuleInfo;
};

struct addi_private {
	/* Pointer to the current process */
	struct task_struct *tsk_Current;

	struct {
		unsigned int ui_Address;
		unsigned char b_BoardVersion;
		unsigned int dw_MolduleConfiguration[4];
	} s_BoardInfos;

	struct {
		unsigned int ul_InterruptOccur;
		unsigned int ui_Read;
		unsigned int ui_Write;
		struct {
			unsigned char b_OldModuleMask;
			unsigned int ul_OldInterruptMask;
			unsigned int ul_OldCounterLatchValue;
		} s_FIFOInterruptParameters[APCI1710_SAVE_INTERRUPT];
	} s_InterruptParameters;

	union str_ModuleInfo s_ModuleInfo[4];
};

static void fpu_begin(void)
{
	kernel_fpu_begin();
}

static void fpu_end(void)
{
	kernel_fpu_end();
}

#include "addi-data/hwdrv_APCI1710.c"

static irqreturn_t v_ADDI_Interrupt(int irq, void *d)
{
	v_APCI1710_Interrupt(irq, d);
	return IRQ_RETVAL(1);
}

static int apci1710_auto_attach(struct comedi_device *dev,
					  unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct addi_private *devpriv;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	devpriv->s_BoardInfos.ui_Address = pci_resource_start(pcidev, 2);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, v_ADDI_Interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	i_ADDI_AttachPCI1710(dev);

	i_APCI1710_Reset(dev);
	return 0;
}

static void apci1710_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		i_APCI1710_Reset(dev);
	if (dev->irq)
		free_irq(dev->irq, dev);
	comedi_pci_disable(dev);
}

static struct comedi_driver apci1710_driver = {
	.driver_name	= "addi_apci_1710",
	.module		= THIS_MODULE,
	.auto_attach	= apci1710_auto_attach,
	.detach		= apci1710_detach,
};

static int apci1710_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci1710_driver, id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(apci1710_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMCC, APCI1710_BOARD_DEVICE_ID) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci1710_pci_table);

static struct pci_driver apci1710_pci_driver = {
	.name		= "addi_apci_1710",
	.id_table	= apci1710_pci_table,
	.probe		= apci1710_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci1710_driver, apci1710_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
