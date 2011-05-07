/*
 * SMC 37C93X initialization code
 */

#include <linux/kernel.h>

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/hwrpb.h>
#include <asm/io.h>
#include <asm/segment.h>

#define SMC_DEBUG 0

#if SMC_DEBUG
# define DBG_DEVS(args)         printk args
#else
# define DBG_DEVS(args)
#endif

#define KB              1024
#define MB              (1024*KB)
#define GB              (1024*MB)

/* device "activate" register contents */
#define DEVICE_ON		1
#define DEVICE_OFF		0

/* configuration on/off keys */
#define CONFIG_ON_KEY		0x55
#define CONFIG_OFF_KEY		0xaa

/* configuration space device definitions */
#define FDC			0
#define IDE1			1
#define IDE2			2
#define PARP			3
#define SER1			4
#define SER2			5
#define RTCL			6
#define KYBD			7
#define AUXIO			8

/* Chip register offsets from base */
#define CONFIG_CONTROL		0x02
#define INDEX_ADDRESS		0x03
#define LOGICAL_DEVICE_NUMBER	0x07
#define DEVICE_ID		0x20
#define DEVICE_REV		0x21
#define POWER_CONTROL		0x22
#define POWER_MGMT		0x23
#define OSC			0x24

#define ACTIVATE		0x30
#define ADDR_HI			0x60
#define ADDR_LO			0x61
#define INTERRUPT_SEL		0x70
#define INTERRUPT_SEL_2		0x72 /* KYBD/MOUS only */
#define DMA_CHANNEL_SEL		0x74 /* FDC/PARP only */

#define FDD_MODE_REGISTER	0x90
#define FDD_OPTION_REGISTER	0x91

/* values that we read back that are expected ... */
#define VALID_DEVICE_ID		2

/* default device addresses */
#define KYBD_INTERRUPT		1
#define MOUS_INTERRUPT		12
#define COM2_BASE		0x2f8
#define COM2_INTERRUPT		3
#define COM1_BASE		0x3f8
#define COM1_INTERRUPT		4
#define PARP_BASE		0x3bc
#define PARP_INTERRUPT		7

static unsigned long __init SMCConfigState(unsigned long baseAddr)
{
	unsigned char devId;

	unsigned long configPort;
	unsigned long indexPort;
	unsigned long dataPort;

	int i;

	configPort = indexPort = baseAddr;
	dataPort = configPort + 1;

#define NUM_RETRIES 5

	for (i = 0; i < NUM_RETRIES; i++)
	{
		outb(CONFIG_ON_KEY, configPort);
		outb(CONFIG_ON_KEY, configPort);
		outb(DEVICE_ID, indexPort);
		devId = inb(dataPort);
		if (devId == VALID_DEVICE_ID) {
			outb(DEVICE_REV, indexPort);
			/* unsigned char devRev = */ inb(dataPort);
			break;
		}
		else
			udelay(100);
	}
	return (i != NUM_RETRIES) ? baseAddr : 0L;
}

static void __init SMCRunState(unsigned long baseAddr)
{
	outb(CONFIG_OFF_KEY, baseAddr);
}

static unsigned long __init SMCDetectUltraIO(void)
{
	unsigned long baseAddr;

	baseAddr = 0x3F0;
	if ( ( baseAddr = SMCConfigState( baseAddr ) ) == 0x3F0 ) {
		return( baseAddr );
	}
	baseAddr = 0x370;
	if ( ( baseAddr = SMCConfigState( baseAddr ) ) == 0x370 ) {
		return( baseAddr );
	}
	return( ( unsigned long )0 );
}

static void __init SMCEnableDevice(unsigned long baseAddr,
			    unsigned long device,
			    unsigned long portaddr,
			    unsigned long interrupt)
{
	unsigned long indexPort;
	unsigned long dataPort;

	indexPort = baseAddr;
	dataPort = baseAddr + 1;

	outb(LOGICAL_DEVICE_NUMBER, indexPort);
	outb(device, dataPort);

	outb(ADDR_LO, indexPort);
	outb(( portaddr & 0xFF ), dataPort);

	outb(ADDR_HI, indexPort);
	outb((portaddr >> 8) & 0xFF, dataPort);

	outb(INTERRUPT_SEL, indexPort);
	outb(interrupt, dataPort);

	outb(ACTIVATE, indexPort);
	outb(DEVICE_ON, dataPort);
}

static void __init SMCEnableKYBD(unsigned long baseAddr)
{
	unsigned long indexPort;
	unsigned long dataPort;

	indexPort = baseAddr;
	dataPort = baseAddr + 1;

	outb(LOGICAL_DEVICE_NUMBER, indexPort);
	outb(KYBD, dataPort);

	outb(INTERRUPT_SEL, indexPort); /* Primary interrupt select */
	outb(KYBD_INTERRUPT, dataPort);

	outb(INTERRUPT_SEL_2, indexPort); /* Secondary interrupt select */
	outb(MOUS_INTERRUPT, dataPort);

	outb(ACTIVATE, indexPort);
	outb(DEVICE_ON, dataPort);
}

static void __init SMCEnableFDC(unsigned long baseAddr)
{
	unsigned long indexPort;
	unsigned long dataPort;

	unsigned char oldValue;

	indexPort = baseAddr;
	dataPort = baseAddr + 1;

	outb(LOGICAL_DEVICE_NUMBER, indexPort);
	outb(FDC, dataPort);

	outb(FDD_MODE_REGISTER, indexPort);
	oldValue = inb(dataPort);

	oldValue |= 0x0E;                   /* Enable burst mode */
	outb(oldValue, dataPort);

	outb(INTERRUPT_SEL, indexPort);	    /* Primary interrupt select */
	outb(0x06, dataPort );

	outb(DMA_CHANNEL_SEL, indexPort);   /* DMA channel select */
	outb(0x02, dataPort);

	outb(ACTIVATE, indexPort);
	outb(DEVICE_ON, dataPort);
}

#if SMC_DEBUG
static void __init SMCReportDeviceStatus(unsigned long baseAddr)
{
	unsigned long indexPort;
	unsigned long dataPort;
	unsigned char currentControl;

	indexPort = baseAddr;
	dataPort = baseAddr + 1;

	outb(POWER_CONTROL, indexPort);
	currentControl = inb(dataPort);

	printk(currentControl & (1 << FDC)
	       ? "\t+FDC Enabled\n" : "\t-FDC Disabled\n");
	printk(currentControl & (1 << IDE1)
	       ? "\t+IDE1 Enabled\n" : "\t-IDE1 Disabled\n");
	printk(currentControl & (1 << IDE2)
	       ? "\t+IDE2 Enabled\n" : "\t-IDE2 Disabled\n");
	printk(currentControl & (1 << PARP)
	       ? "\t+PARP Enabled\n" : "\t-PARP Disabled\n");
	printk(currentControl & (1 << SER1)
	       ? "\t+SER1 Enabled\n" : "\t-SER1 Disabled\n");
	printk(currentControl & (1 << SER2)
	       ? "\t+SER2 Enabled\n" : "\t-SER2 Disabled\n");

	printk( "\n" );
}
#endif

int __init SMC93x_Init(void)
{
	unsigned long SMCUltraBase;
	unsigned long flags;

	local_irq_save(flags);
	if ((SMCUltraBase = SMCDetectUltraIO()) != 0UL) {
#if SMC_DEBUG
		SMCReportDeviceStatus(SMCUltraBase);
#endif
		SMCEnableDevice(SMCUltraBase, SER1, COM1_BASE, COM1_INTERRUPT);
		DBG_DEVS(("SMC FDC37C93X: SER1 done\n"));
		SMCEnableDevice(SMCUltraBase, SER2, COM2_BASE, COM2_INTERRUPT);
		DBG_DEVS(("SMC FDC37C93X: SER2 done\n"));
		SMCEnableDevice(SMCUltraBase, PARP, PARP_BASE, PARP_INTERRUPT);
		DBG_DEVS(("SMC FDC37C93X: PARP done\n"));
		/* On PC164, IDE on the SMC is not enabled;
		   CMD646 (PCI) on MB */
		SMCEnableKYBD(SMCUltraBase);
		DBG_DEVS(("SMC FDC37C93X: KYB done\n"));
		SMCEnableFDC(SMCUltraBase);
		DBG_DEVS(("SMC FDC37C93X: FDC done\n"));
#if SMC_DEBUG
		SMCReportDeviceStatus(SMCUltraBase);
#endif
		SMCRunState(SMCUltraBase);
		local_irq_restore(flags);
		printk("SMC FDC37C93X Ultra I/O Controller found @ 0x%lx\n",
		       SMCUltraBase);
		return 1;
	}
	else {
		local_irq_restore(flags);
		DBG_DEVS(("No SMC FDC37C93X Ultra I/O Controller found\n"));
		return 0;
	}
}
