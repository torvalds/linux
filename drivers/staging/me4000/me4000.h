/*
 * Copyright (C) 2003 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 * Source File : me4000.h
 * Author      : GG (Guenter Gebhardt)  <g.gebhardt@meilhaus.de>
 */

#ifndef _ME4000_H_
#define _ME4000_H_

#ifdef __KERNEL__

/*=============================================================================
  The version of the driver release
  ===========================================================================*/

#define ME4000_DRIVER_VERSION  0x10009	// Version 1.00.09

/*=============================================================================
  Debug section
  ===========================================================================*/

#undef ME4000_CALL_DEBUG	// Debug function entry and exit
#undef ME4000_ISR_DEBUG		// Debug the interrupt service routine
#undef ME4000_PORT_DEBUG	// Debug port access
#undef ME4000_DEBUG		// General purpose debug masseges

#ifdef ME4000_CALL_DEBUG
#undef CALL_PDEBUG
#define CALL_PDEBUG(fmt, args...) printk(KERN_DEBUG"ME4000:" fmt, ##args)
#else
# define CALL_PDEBUG(fmt, args...)	// no debugging, do nothing
#endif

#ifdef ME4000_ISR_DEBUG
#undef ISR_PDEBUG
#define ISR_PDEBUG(fmt, args...) printk(KERN_DEBUG"ME4000:" fmt, ##args)
#else
#define ISR_PDEBUG(fmt, args...)	// no debugging, do nothing
#endif

#ifdef ME4000_PORT_DEBUG
#undef PORT_PDEBUG
#define PORT_PDEBUG(fmt, args...) printk(KERN_DEBUG"ME4000:" fmt, ##args)
#else
#define PORT_PDEBUG(fmt, args...)	// no debugging, do nothing
#endif

#ifdef ME4000_DEBUG
#undef PDEBUG
#define PDEBUG(fmt, args...) printk(KERN_DEBUG"ME4000:" fmt, ##args)
#else
#define PDEBUG(fmt, args...)	// no debugging, do nothing
#endif

/*=============================================================================
  PCI vendor and device IDs
  ===========================================================================*/

#define PCI_VENDOR_ID_MEILHAUS 0x1402

#define PCI_DEVICE_ID_MEILHAUS_ME4650	0x4650	// Low Cost version

#define PCI_DEVICE_ID_MEILHAUS_ME4660	0x4660	// Standard version
#define PCI_DEVICE_ID_MEILHAUS_ME4660I	0x4661	// Isolated version
#define PCI_DEVICE_ID_MEILHAUS_ME4660S	0x4662	// Standard version with Sample and Hold
#define PCI_DEVICE_ID_MEILHAUS_ME4660IS	0x4663	// Isolated version with Sample and Hold

#define PCI_DEVICE_ID_MEILHAUS_ME4670	0x4670	// Standard version
#define PCI_DEVICE_ID_MEILHAUS_ME4670I	0x4671	// Isolated version
#define PCI_DEVICE_ID_MEILHAUS_ME4670S	0x4672	// Standard version with Sample and Hold
#define PCI_DEVICE_ID_MEILHAUS_ME4670IS	0x4673	// Isolated version with Sample and Hold

#define PCI_DEVICE_ID_MEILHAUS_ME4680	0x4680	// Standard version
#define PCI_DEVICE_ID_MEILHAUS_ME4680I	0x4681	// Isolated version
#define PCI_DEVICE_ID_MEILHAUS_ME4680S	0x4682	// Standard version with Sample and Hold
#define PCI_DEVICE_ID_MEILHAUS_ME4680IS	0x4683	// Isolated version with Sample and Hold

/*=============================================================================
  Device names, for entries in /proc/..
  ===========================================================================*/

#define ME4000_NAME		"me4000"
#define ME4000_AO_NAME		"me4000_ao"
#define ME4000_AI_NAME		"me4000_ai"
#define ME4000_DIO_NAME		"me4000_dio"
#define ME4000_CNT_NAME		"me4000_cnt"
#define ME4000_EXT_INT_NAME	"me4000_ext_int"

/*=============================================================================
  ME-4000 base register offsets
  ===========================================================================*/

#define ME4000_AO_00_CTRL_REG			0x00	// R/W
#define ME4000_AO_00_STATUS_REG			0x04	// R/_
#define ME4000_AO_00_FIFO_REG			0x08	// _/W
#define ME4000_AO_00_SINGLE_REG			0x0C	// R/W
#define ME4000_AO_00_TIMER_REG			0x10	// _/W

#define ME4000_AO_01_CTRL_REG			0x18	// R/W
#define ME4000_AO_01_STATUS_REG			0x1C	// R/_
#define ME4000_AO_01_FIFO_REG			0x20	// _/W
#define ME4000_AO_01_SINGLE_REG			0x24	// R/W
#define ME4000_AO_01_TIMER_REG			0x28	// _/W

#define ME4000_AO_02_CTRL_REG			0x30	// R/W
#define ME4000_AO_02_STATUS_REG			0x34	// R/_
#define ME4000_AO_02_FIFO_REG			0x38	// _/W
#define ME4000_AO_02_SINGLE_REG			0x3C	// R/W
#define ME4000_AO_02_TIMER_REG			0x40	// _/W

#define ME4000_AO_03_CTRL_REG			0x48	// R/W
#define ME4000_AO_03_STATUS_REG			0x4C	// R/_
#define ME4000_AO_03_FIFO_REG			0x50	// _/W
#define ME4000_AO_03_SINGLE_REG			0x54	// R/W
#define ME4000_AO_03_TIMER_REG			0x58	// _/W

#define ME4000_AI_CTRL_REG			0x74	// _/W
#define ME4000_AI_STATUS_REG			0x74	// R/_
#define ME4000_AI_CHANNEL_LIST_REG		0x78	// _/W
#define ME4000_AI_DATA_REG			0x7C	// R/_
#define ME4000_AI_CHAN_TIMER_REG		0x80	// _/W
#define ME4000_AI_CHAN_PRE_TIMER_REG		0x84	// _/W
#define ME4000_AI_SCAN_TIMER_LOW_REG		0x88	// _/W
#define ME4000_AI_SCAN_TIMER_HIGH_REG		0x8C	// _/W
#define ME4000_AI_SCAN_PRE_TIMER_LOW_REG	0x90	// _/W
#define ME4000_AI_SCAN_PRE_TIMER_HIGH_REG	0x94	// _/W
#define ME4000_AI_START_REG			0x98	// R/_

#define ME4000_IRQ_STATUS_REG			0x9C	// R/_

#define ME4000_DIO_PORT_0_REG			0xA0	// R/W
#define ME4000_DIO_PORT_1_REG			0xA4	// R/W
#define ME4000_DIO_PORT_2_REG			0xA8	// R/W
#define ME4000_DIO_PORT_3_REG			0xAC	// R/W
#define ME4000_DIO_DIR_REG			0xB0	// R/W

#define ME4000_AO_LOADSETREG_XX			0xB4	// R/W

#define ME4000_DIO_CTRL_REG			0xB8	// R/W

#define ME4000_AO_DEMUX_ADJUST_REG		0xBC	// -/W

#define ME4000_AI_SAMPLE_COUNTER_REG		0xC0	// _/W

/*=============================================================================
  Value to adjust Demux
  ===========================================================================*/

#define ME4000_AO_DEMUX_ADJUST_VALUE            0x4C

/*=============================================================================
  Counter base register offsets
  ===========================================================================*/

#define ME4000_CNT_COUNTER_0_REG		0x00
#define ME4000_CNT_COUNTER_1_REG		0x01
#define ME4000_CNT_COUNTER_2_REG		0x02
#define ME4000_CNT_CTRL_REG			0x03

/*=============================================================================
  PLX base register offsets
  ===========================================================================*/

#define PLX_INTCSR	0x4C	// Interrupt control and status register
#define PLX_ICR		0x50	// Initialization control register

/*=============================================================================
  Bits for the PLX_ICSR register
  ===========================================================================*/

#define PLX_INTCSR_LOCAL_INT1_EN             0x01	// If set, local interrupt 1 is enabled (r/w)
#define PLX_INTCSR_LOCAL_INT1_POL            0x02	// If set, local interrupt 1 polarity is active high (r/w)
#define PLX_INTCSR_LOCAL_INT1_STATE          0x04	// If set, local interrupt 1 is active (r/_)
#define PLX_INTCSR_LOCAL_INT2_EN             0x08	// If set, local interrupt 2 is enabled (r/w)
#define PLX_INTCSR_LOCAL_INT2_POL            0x10	// If set, local interrupt 2 polarity is active high (r/w)
#define PLX_INTCSR_LOCAL_INT2_STATE          0x20	// If set, local interrupt 2 is active  (r/_)
#define PLX_INTCSR_PCI_INT_EN                0x40	// If set, PCI interrupt is enabled (r/w)
#define PLX_INTCSR_SOFT_INT                  0x80	// If set, a software interrupt is generated (r/w)

/*=============================================================================
  Bits for the PLX_ICR register
  ===========================================================================*/

#define PLX_ICR_BIT_EEPROM_CLOCK_SET		0x01000000
#define PLX_ICR_BIT_EEPROM_CHIP_SELECT		0x02000000
#define PLX_ICR_BIT_EEPROM_WRITE		0x04000000
#define PLX_ICR_BIT_EEPROM_READ			0x08000000
#define PLX_ICR_BIT_EEPROM_VALID		0x10000000

#define PLX_ICR_MASK_EEPROM			0x1F000000

#define EEPROM_DELAY				1

/*=============================================================================
  Bits for the ME4000_AO_CTRL_REG register
  ===========================================================================*/

#define ME4000_AO_CTRL_BIT_MODE_0		0x001
#define ME4000_AO_CTRL_BIT_MODE_1		0x002
#define ME4000_AO_CTRL_MASK_MODE		0x003
#define ME4000_AO_CTRL_BIT_STOP			0x004
#define ME4000_AO_CTRL_BIT_ENABLE_FIFO		0x008
#define ME4000_AO_CTRL_BIT_ENABLE_EX_TRIG	0x010
#define ME4000_AO_CTRL_BIT_EX_TRIG_EDGE		0x020
#define ME4000_AO_CTRL_BIT_IMMEDIATE_STOP	0x080
#define ME4000_AO_CTRL_BIT_ENABLE_DO		0x100
#define ME4000_AO_CTRL_BIT_ENABLE_IRQ		0x200
#define ME4000_AO_CTRL_BIT_RESET_IRQ		0x400
#define ME4000_AO_CTRL_BIT_EX_TRIG_BOTH		0x800

/*=============================================================================
  Bits for the ME4000_AO_STATUS_REG register
  ===========================================================================*/

#define ME4000_AO_STATUS_BIT_FSM		0x01
#define ME4000_AO_STATUS_BIT_FF			0x02
#define ME4000_AO_STATUS_BIT_HF			0x04
#define ME4000_AO_STATUS_BIT_EF			0x08

/*=============================================================================
  Bits for the ME4000_AI_CTRL_REG register
  ===========================================================================*/

#define ME4000_AI_CTRL_BIT_MODE_0		0x00000001
#define ME4000_AI_CTRL_BIT_MODE_1		0x00000002
#define ME4000_AI_CTRL_BIT_MODE_2		0x00000004
#define ME4000_AI_CTRL_BIT_SAMPLE_HOLD		0x00000008
#define ME4000_AI_CTRL_BIT_IMMEDIATE_STOP	0x00000010
#define ME4000_AI_CTRL_BIT_STOP			0x00000020
#define ME4000_AI_CTRL_BIT_CHANNEL_FIFO		0x00000040
#define ME4000_AI_CTRL_BIT_DATA_FIFO		0x00000080
#define ME4000_AI_CTRL_BIT_FULLSCALE		0x00000100
#define ME4000_AI_CTRL_BIT_OFFSET		0x00000200
#define ME4000_AI_CTRL_BIT_EX_TRIG_ANALOG	0x00000400
#define ME4000_AI_CTRL_BIT_EX_TRIG		0x00000800
#define ME4000_AI_CTRL_BIT_EX_TRIG_FALLING	0x00001000
#define ME4000_AI_CTRL_BIT_EX_IRQ		0x00002000
#define ME4000_AI_CTRL_BIT_EX_IRQ_RESET		0x00004000
#define ME4000_AI_CTRL_BIT_LE_IRQ		0x00008000
#define ME4000_AI_CTRL_BIT_LE_IRQ_RESET		0x00010000
#define ME4000_AI_CTRL_BIT_HF_IRQ		0x00020000
#define ME4000_AI_CTRL_BIT_HF_IRQ_RESET		0x00040000
#define ME4000_AI_CTRL_BIT_SC_IRQ		0x00080000
#define ME4000_AI_CTRL_BIT_SC_IRQ_RESET		0x00100000
#define ME4000_AI_CTRL_BIT_SC_RELOAD		0x00200000
#define ME4000_AI_CTRL_BIT_EX_TRIG_BOTH		0x80000000

/*=============================================================================
  Bits for the ME4000_AI_STATUS_REG register
  ===========================================================================*/

#define ME4000_AI_STATUS_BIT_EF_CHANNEL		0x00400000
#define ME4000_AI_STATUS_BIT_HF_CHANNEL		0x00800000
#define ME4000_AI_STATUS_BIT_FF_CHANNEL		0x01000000
#define ME4000_AI_STATUS_BIT_EF_DATA		0x02000000
#define ME4000_AI_STATUS_BIT_HF_DATA		0x04000000
#define ME4000_AI_STATUS_BIT_FF_DATA		0x08000000
#define ME4000_AI_STATUS_BIT_LE			0x10000000
#define ME4000_AI_STATUS_BIT_FSM		0x20000000

/*=============================================================================
  Bits for the ME4000_IRQ_STATUS_REG register
  ===========================================================================*/

#define ME4000_IRQ_STATUS_BIT_EX		0x01
#define ME4000_IRQ_STATUS_BIT_LE		0x02
#define ME4000_IRQ_STATUS_BIT_AI_HF		0x04
#define ME4000_IRQ_STATUS_BIT_AO_0_HF		0x08
#define ME4000_IRQ_STATUS_BIT_AO_1_HF		0x10
#define ME4000_IRQ_STATUS_BIT_AO_2_HF		0x20
#define ME4000_IRQ_STATUS_BIT_AO_3_HF		0x40
#define ME4000_IRQ_STATUS_BIT_SC		0x80

/*=============================================================================
  Bits for the ME4000_DIO_CTRL_REG register
  ===========================================================================*/

#define ME4000_DIO_CTRL_BIT_MODE_0		0X0001
#define ME4000_DIO_CTRL_BIT_MODE_1		0X0002
#define ME4000_DIO_CTRL_BIT_MODE_2		0X0004
#define ME4000_DIO_CTRL_BIT_MODE_3		0X0008
#define ME4000_DIO_CTRL_BIT_MODE_4		0X0010
#define ME4000_DIO_CTRL_BIT_MODE_5		0X0020
#define ME4000_DIO_CTRL_BIT_MODE_6		0X0040
#define ME4000_DIO_CTRL_BIT_MODE_7		0X0080

#define ME4000_DIO_CTRL_BIT_FUNCTION_0		0X0100
#define ME4000_DIO_CTRL_BIT_FUNCTION_1		0X0200

#define ME4000_DIO_CTRL_BIT_FIFO_HIGH_0		0X0400
#define ME4000_DIO_CTRL_BIT_FIFO_HIGH_1		0X0800
#define ME4000_DIO_CTRL_BIT_FIFO_HIGH_2		0X1000
#define ME4000_DIO_CTRL_BIT_FIFO_HIGH_3		0X2000

/*=============================================================================
  Bits for the ME4000_CNT_CTRL_REG register
  ===========================================================================*/

#define ME4000_CNT_CTRL_BIT_COUNTER_0  0x00
#define ME4000_CNT_CTRL_BIT_COUNTER_1  0x40
#define ME4000_CNT_CTRL_BIT_COUNTER_2  0x80

#define ME4000_CNT_CTRL_BIT_MODE_0     0x00	// Change state if zero crossing
#define ME4000_CNT_CTRL_BIT_MODE_1     0x02	// Retriggerable One-Shot
#define ME4000_CNT_CTRL_BIT_MODE_2     0x04	// Asymmetrical divider
#define ME4000_CNT_CTRL_BIT_MODE_3     0x06	// Symmetrical divider
#define ME4000_CNT_CTRL_BIT_MODE_4     0x08	// Counter start by software trigger
#define ME4000_CNT_CTRL_BIT_MODE_5     0x0A	// Counter start by hardware trigger

/*=============================================================================
  Extract information from minor device number
  ===========================================================================*/

#define AO_BOARD(dev) ((MINOR(dev) >> 6) & 0x3)
#define AO_PORT(dev)  ((MINOR(dev) >> 2) & 0xF)
#define AO_MODE(dev)  (MINOR(dev) & 0x3)

#define AI_BOARD(dev) ((MINOR(dev) >> 3) & 0x1F)
#define AI_MODE(dev)  (MINOR(dev) & 0x7)

#define DIO_BOARD(dev) (MINOR(dev))

#define CNT_BOARD(dev) (MINOR(dev))

#define EXT_INT_BOARD(dev) (MINOR(dev))

/*=============================================================================
  Circular buffer used for analog input/output reads/writes.
  ===========================================================================*/

struct me4000_circ_buf {
	s16 *buf;
	int volatile head;
	int volatile tail;
};

/*=============================================================================
  Information about the hardware capabilities
  ===========================================================================*/

struct me4000_ao_info {
	int count;
	int fifo_count;
};

struct me4000_ai_info {
	int count;
	int sh_count;
	int diff_count;
	int ex_trig_analog;
};

struct me4000_dio_info {
	int count;
};

struct me4000_cnt_info {
	int count;
};

struct me4000_board {
	u16 vendor_id;
	u16 device_id;
	struct me4000_ao_info ao;
	struct me4000_ai_info ai;
	struct me4000_dio_info dio;
	struct me4000_cnt_info cnt;
};

static struct me4000_board me4000_boards[] = {
	{PCI_VENDOR_ID_MEILHAUS, 0x4610, {0, 0}, {16, 0, 0, 0}, {4}, {3}},

	{PCI_VENDOR_ID_MEILHAUS, 0x4650, {0, 0}, {16, 0, 0, 0}, {4}, {0}},

	{PCI_VENDOR_ID_MEILHAUS, 0x4660, {2, 0}, {16, 0, 0, 0}, {4}, {3}},
	{PCI_VENDOR_ID_MEILHAUS, 0x4661, {2, 0}, {16, 0, 0, 0}, {4}, {3}},
	{PCI_VENDOR_ID_MEILHAUS, 0x4662, {2, 0}, {16, 8, 0, 0}, {4}, {3}},
	{PCI_VENDOR_ID_MEILHAUS, 0x4663, {2, 0}, {16, 8, 0, 0}, {4}, {3}},

	{PCI_VENDOR_ID_MEILHAUS, 0x4670, {4, 0}, {32, 0, 16, 1}, {4}, {3}},
	{PCI_VENDOR_ID_MEILHAUS, 0x4671, {4, 0}, {32, 0, 16, 1}, {4}, {3}},
	{PCI_VENDOR_ID_MEILHAUS, 0x4672, {4, 0}, {32, 8, 16, 1}, {4}, {3}},
	{PCI_VENDOR_ID_MEILHAUS, 0x4673, {4, 0}, {32, 8, 16, 1}, {4}, {3}},

	{PCI_VENDOR_ID_MEILHAUS, 0x4680, {4, 4}, {32, 0, 16, 1}, {4}, {3}},
	{PCI_VENDOR_ID_MEILHAUS, 0x4681, {4, 4}, {32, 0, 16, 1}, {4}, {3}},
	{PCI_VENDOR_ID_MEILHAUS, 0x4682, {4, 4}, {32, 8, 16, 1}, {4}, {3}},
	{PCI_VENDOR_ID_MEILHAUS, 0x4683, {4, 4}, {32, 8, 16, 1}, {4}, {3}},

	{0},
};

/*=============================================================================
  PCI device table.
  This is used by modprobe to translate PCI IDs to drivers.
  ===========================================================================*/

static struct pci_device_id me4000_pci_table[] __devinitdata = {
	{PCI_VENDOR_ID_MEILHAUS, 0x4610, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, 0x4650, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, 0x4660, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, 0x4661, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, 0x4662, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, 0x4663, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, 0x4670, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, 0x4671, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, 0x4672, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, 0x4673, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, 0x4680, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, 0x4681, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, 0x4682, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, 0x4683, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},

	{0}
};

MODULE_DEVICE_TABLE(pci, me4000_pci_table);

/*=============================================================================
  Global board and subdevice information structures
  ===========================================================================*/

struct me4000_info {
	struct list_head list;	// List of all detected boards
	int board_count;	// Index of the board after detection

	unsigned long plx_regbase;	// PLX configuration space base address
	resource_size_t me4000_regbase;	// Base address of the ME4000
	resource_size_t timer_regbase;	// Base address of the timer circuit
	resource_size_t program_regbase;	// Base address to set the program pin for the xilinx

	unsigned long plx_regbase_size;	// PLX register set space
	resource_size_t me4000_regbase_size;	// ME4000 register set space
	resource_size_t timer_regbase_size;	// Timer circuit register set space
	resource_size_t program_regbase_size;	// Size of program base address of the ME4000

	unsigned int serial_no;	// Serial number of the board
	unsigned char hw_revision;	// Hardware revision of the board
	unsigned short vendor_id;	// Meilhaus vendor id (0x1402)
	unsigned short device_id;	// Device ID

	int pci_bus_no;		// PCI bus number
	int pci_dev_no;		// PCI device number
	int pci_func_no;	// PCI function number
	struct pci_dev *pci_dev_p;	// General PCI information

	struct me4000_board *board_p;	// Holds the board capabilities

	unsigned int irq;	// IRQ assigned from the PCI BIOS
	unsigned int irq_count;	// Count of external interrupts

	spinlock_t preload_lock;	// Guards the analog output preload register
	spinlock_t ai_ctrl_lock;	// Guards the analog input control register

	struct list_head ao_context_list;	// List with analog output specific context
	struct me4000_ai_context *ai_context;	// Analog input  specific context
	struct me4000_dio_context *dio_context;	// Digital I/O specific context
	struct me4000_cnt_context *cnt_context;	// Counter specific context
	struct me4000_ext_int_context *ext_int_context;	// External interrupt specific context
};

struct me4000_ao_context {
	struct list_head list;	// linked list of me4000_ao_context_t
	int index;		// Index in the list
	int mode;		// Indicates mode (0 = single, 1 = wraparound, 2 = continous)
	int dac_in_use;		// Indicates if already opend
	spinlock_t use_lock;	// Guards in_use
	spinlock_t int_lock;	// Used when locking out interrupts
	struct me4000_circ_buf circ_buf;	// Circular buffer
	wait_queue_head_t wait_queue;	// Wait queue to sleep while blocking write
	struct me4000_info *board_info;
	unsigned int irq;	// The irq associated with this ADC
	int volatile pipe_flag;	// Indicates broken pipe set from me4000_ao_isr()
	unsigned long ctrl_reg;
	unsigned long status_reg;
	unsigned long fifo_reg;
	unsigned long single_reg;
	unsigned long timer_reg;
	unsigned long irq_status_reg;
	unsigned long preload_reg;
	struct fasync_struct *fasync_p;	// Queue for asynchronous notification
};

struct me4000_ai_context {
	struct list_head list;	// linked list of me4000_ai_info_t
	int mode;		// Indicates mode
	int in_use;		// Indicates if already opend
	spinlock_t use_lock;	// Guards in_use
	spinlock_t int_lock;	// Used when locking out interrupts
	int number;		// Number of the DAC
	unsigned int irq;	// The irq associated with this ADC
	struct me4000_circ_buf circ_buf;	// Circular buffer
	wait_queue_head_t wait_queue;	// Wait queue to sleep while blocking read
	struct me4000_info *board_info;

	struct fasync_struct *fasync_p;	// Queue for asynchronous notification

	unsigned long ctrl_reg;
	unsigned long status_reg;
	unsigned long channel_list_reg;
	unsigned long data_reg;
	unsigned long chan_timer_reg;
	unsigned long chan_pre_timer_reg;
	unsigned long scan_timer_low_reg;
	unsigned long scan_timer_high_reg;
	unsigned long scan_pre_timer_low_reg;
	unsigned long scan_pre_timer_high_reg;
	unsigned long start_reg;
	unsigned long irq_status_reg;
	unsigned long sample_counter_reg;

	unsigned long chan_timer;
	unsigned long chan_pre_timer;
	unsigned long scan_timer_low;
	unsigned long scan_timer_high;
	unsigned long channel_list_count;
	unsigned long sample_counter;
	int sample_counter_reload;
};

struct me4000_dio_context {
	struct list_head list;	// linked list of me4000_dio_context_t
	int in_use;		// Indicates if already opend
	spinlock_t use_lock;	// Guards in_use
	int number;
	int dio_count;
	struct me4000_info *board_info;
	unsigned long dir_reg;
	unsigned long ctrl_reg;
	unsigned long port_0_reg;
	unsigned long port_1_reg;
	unsigned long port_2_reg;
	unsigned long port_3_reg;
};

struct me4000_cnt_context {
	struct list_head list;	// linked list of me4000_dio_context_t
	int in_use;		// Indicates if already opend
	spinlock_t use_lock;	// Guards in_use
	int number;
	int cnt_count;
	struct me4000_info *board_info;
	unsigned long ctrl_reg;
	unsigned long counter_0_reg;
	unsigned long counter_1_reg;
	unsigned long counter_2_reg;
};

struct me4000_ext_int_context {
	struct list_head list;	// linked list of me4000_dio_context_t
	int in_use;		// Indicates if already opend
	spinlock_t use_lock;	// Guards in_use
	int number;
	struct me4000_info *board_info;
	unsigned int irq;
	unsigned long int_count;
	struct fasync_struct *fasync_ptr;
	unsigned long ctrl_reg;
	unsigned long irq_status_reg;
};

#endif

/*=============================================================================
  Application include section starts here
  ===========================================================================*/

/*-----------------------------------------------------------------------------
  Defines for analog input
  ----------------------------------------------------------------------------*/

/* General stuff */
#define ME4000_AI_FIFO_COUNT		2048

#define ME4000_AI_MIN_TICKS		66
#define ME4000_AI_MAX_SCAN_TICKS	0xFFFFFFFFFFLL

#define ME4000_AI_BUFFER_SIZE 		(32 * 1024)	// Size in bytes

#define ME4000_AI_BUFFER_COUNT		((ME4000_AI_BUFFER_SIZE) / 2)	// Size in values

/* Channel list defines and masks */
#define ME4000_AI_CHANNEL_LIST_COUNT		1024

#define ME4000_AI_LIST_INPUT_SINGLE_ENDED	0x000
#define ME4000_AI_LIST_INPUT_DIFFERENTIAL	0x020

#define ME4000_AI_LIST_RANGE_BIPOLAR_10		0x000
#define ME4000_AI_LIST_RANGE_BIPOLAR_2_5	0x040
#define ME4000_AI_LIST_RANGE_UNIPOLAR_10	0x080
#define ME4000_AI_LIST_RANGE_UNIPOLAR_2_5	0x0C0

#define ME4000_AI_LIST_LAST_ENTRY		0x100

/* External trigger defines */
#define ME4000_AI_TRIGGER_SOFTWARE		0x0	// Use only with API
#define ME4000_AI_TRIGGER_EXT_DIGITAL		0x1
#define ME4000_AI_TRIGGER_EXT_ANALOG		0x2

#define ME4000_AI_TRIGGER_EXT_EDGE_RISING	0x0
#define ME4000_AI_TRIGGER_EXT_EDGE_FALLING	0x1
#define ME4000_AI_TRIGGER_EXT_EDGE_BOTH		0x2

/* Sample and Hold */
#define ME4000_AI_SIMULTANEOUS_DISABLE		0x0
#define ME4000_AI_SIMULTANEOUS_ENABLE		0x1

/* Defines for the Sample Counter */
#define ME4000_AI_SC_RELOAD			0x0
#define ME4000_AI_SC_ONCE			0x1

/* Modes for analog input */
#define ME4000_AI_ACQ_MODE_SINGLE		0x00	// Catch one single value
#define ME4000_AI_ACQ_MODE_SOFTWARE		0x01	// Continous sampling with software start
#define ME4000_AI_ACQ_MODE_EXT			0x02	// Continous sampling with external trigger start
#define ME4000_AI_ACQ_MODE_EXT_SINGLE_VALUE	0x03	// Sample one value by external trigger
#define ME4000_AI_ACQ_MODE_EXT_SINGLE_CHANLIST	0x04	// Sample one channel list by external trigger

/* Staus of AI FSM */
#define ME4000_AI_STATUS_IDLE			0x0
#define ME4000_AI_STATUS_BUSY			0x1

/* Voltages for calibration */
#define ME4000_AI_GAIN_1_UNI_OFFSET		10.0E-3
#define ME4000_AI_GAIN_1_UNI_FULLSCALE		9950.0E-3
#define ME4000_AI_GAIN_1_BI_OFFSET		0.0
#define ME4000_AI_GAIN_1_BI_FULLSCALE		9950.0E-3
#define ME4000_AI_GAIN_4_UNI_OFFSET		10.0E-3
#define ME4000_AI_GAIN_4_UNI_FULLSCALE		2450.0E-3
#define ME4000_AI_GAIN_4_BI_OFFSET		0.0
#define ME4000_AI_GAIN_4_BI_FULLSCALE		2450.0E-3

/* Ideal digits for calibration */
#define ME4000_AI_GAIN_1_UNI_OFFSET_DIGITS	(-32702)
#define ME4000_AI_GAIN_1_UNI_FULLSCALE_DIGITS	32440
#define ME4000_AI_GAIN_1_BI_OFFSET_DIGITS	0
#define ME4000_AI_GAIN_1_BI_FULLSCALE_DIGITS	32604
#define ME4000_AI_GAIN_4_UNI_OFFSET_DIGITS	(-32505)
#define ME4000_AI_GAIN_4_UNI_FULLSCALE_DIGITS	31457
#define ME4000_AI_GAIN_4_BI_OFFSET_DIGITS	0
#define ME4000_AI_GAIN_4_BI_FULLSCALE_DIGITS	32113

/*-----------------------------------------------------------------------------
  Defines for analog output
  ----------------------------------------------------------------------------*/

/* General stuff */
#define ME4000_AO_FIFO_COUNT			(4 * 1024)

#define ME4000_AO_MIN_TICKS			66

#define ME4000_AO_BUFFER_SIZE 			(32 * 1024)	// Size in bytes

#define ME4000_AO_BUFFER_COUNT 			((ME4000_AO_BUFFER_SIZE) / 2)	// Size in values

/* Conversion modes for analog output */
#define ME4000_AO_CONV_MODE_SINGLE		0x0
#define ME4000_AO_CONV_MODE_WRAPAROUND		0x1
#define ME4000_AO_CONV_MODE_CONTINUOUS		0x2

/* Trigger setup */
#define ME4000_AO_TRIGGER_EXT_EDGE_RISING	0x0
#define ME4000_AO_TRIGGER_EXT_EDGE_FALLING	0x1
#define ME4000_AO_TRIGGER_EXT_EDGE_BOTH		0x2

/* Status of AO FSM */
#define ME4000_AO_STATUS_IDLE			0x0
#define ME4000_AO_STATUS_BUSY			0x1

/*-----------------------------------------------------------------------------
  Defines for eeprom
  ----------------------------------------------------------------------------*/

#define ME4000_EEPROM_CMD_READ			0x180
#define ME4000_EEPROM_CMD_WRITE_ENABLE		0x130
#define ME4000_EEPROM_CMD_WRITE_DISABLE		0x100
#define ME4000_EEPROM_CMD_WRITE			0x1400000

#define ME4000_EEPROM_CMD_LENGTH_READ		9
#define ME4000_EEPROM_CMD_LENGTH_WRITE_ENABLE	9
#define ME4000_EEPROM_CMD_LENGTH_WRITE_DISABLE	9
#define ME4000_EEPROM_CMD_LENGTH_WRITE		25

#define ME4000_EEPROM_ADR_DATE_HIGH		0x32
#define ME4000_EEPROM_ADR_DATE_LOW		0x33

#define ME4000_EEPROM_ADR_GAIN_1_UNI_OFFSET	0x34
#define ME4000_EEPROM_ADR_GAIN_1_UNI_FULLSCALE	0x35
#define ME4000_EEPROM_ADR_GAIN_1_BI_OFFSET	0x36
#define ME4000_EEPROM_ADR_GAIN_1_BI_FULLSCALE	0x37
#define ME4000_EEPROM_ADR_GAIN_1_DIFF_OFFSET	0x38
#define ME4000_EEPROM_ADR_GAIN_1_DIFF_FULLSCALE	0x39

#define ME4000_EEPROM_ADR_GAIN_4_UNI_OFFSET	0x3A
#define ME4000_EEPROM_ADR_GAIN_4_UNI_FULLSCALE	0x3B
#define ME4000_EEPROM_ADR_GAIN_4_BI_OFFSET	0x3C
#define ME4000_EEPROM_ADR_GAIN_4_BI_FULLSCALE	0x3D
#define ME4000_EEPROM_ADR_GAIN_4_DIFF_OFFSET	0x3E
#define ME4000_EEPROM_ADR_GAIN_4_DIFF_FULLSCALE	0x3F

#define ME4000_EEPROM_ADR_LENGTH		6
#define ME4000_EEPROM_DATA_LENGTH		16

/*-----------------------------------------------------------------------------
  Defines for digital I/O
  ----------------------------------------------------------------------------*/

#define ME4000_DIO_PORT_A		0x0
#define ME4000_DIO_PORT_B		0x1
#define ME4000_DIO_PORT_C		0x2
#define ME4000_DIO_PORT_D		0x3

#define ME4000_DIO_PORT_INPUT		0x0
#define ME4000_DIO_PORT_OUTPUT		0x1
#define ME4000_DIO_FIFO_LOW		0x2
#define ME4000_DIO_FIFO_HIGH		0x3

#define ME4000_DIO_FUNCTION_PATTERN	0x0
#define ME4000_DIO_FUNCTION_DEMUX	0x1
#define ME4000_DIO_FUNCTION_MUX		0x2

/*-----------------------------------------------------------------------------
  Defines for counters
  ----------------------------------------------------------------------------*/

#define ME4000_CNT_COUNTER_0  0
#define ME4000_CNT_COUNTER_1  1
#define ME4000_CNT_COUNTER_2  2

#define ME4000_CNT_MODE_0     0	// Change state if zero crossing
#define ME4000_CNT_MODE_1     1	// Retriggerable One-Shot
#define ME4000_CNT_MODE_2     2	// Asymmetrical divider
#define ME4000_CNT_MODE_3     3	// Symmetrical divider
#define ME4000_CNT_MODE_4     4	// Counter start by software trigger
#define ME4000_CNT_MODE_5     5	// Counter start by hardware trigger

/*-----------------------------------------------------------------------------
  General type definitions
  ----------------------------------------------------------------------------*/

struct me4000_user_info {
	int board_count;	// Index of the board after detection
	unsigned long plx_regbase;	// PLX configuration space base address
	resource_size_t me4000_regbase;	// Base address of the ME4000
	unsigned long plx_regbase_size;	// PLX register set space
	resource_size_t me4000_regbase_size;	// ME4000 register set space
	unsigned long serial_no;	// Serial number of the board
	unsigned char hw_revision;	// Hardware revision of the board
	unsigned short vendor_id;	// Meilhaus vendor id (0x1402)
	unsigned short device_id;	// Device ID
	int pci_bus_no;		// PCI bus number
	int pci_dev_no;		// PCI device number
	int pci_func_no;	// PCI function number
	char irq;		// IRQ assigned from the PCI BIOS
	int irq_count;		// Count of external interrupts

	int driver_version;	// Version of the driver release

	int ao_count;		// Count of analog output channels
	int ao_fifo_count;	// Count fo analog output fifos

	int ai_count;		// Count of analog input channels
	int ai_sh_count;	// Count of sample and hold devices
	int ai_ex_trig_analog;	// Flag to indicate if analogous external trigger is available

	int dio_count;		// Count of digital I/O ports

	int cnt_count;		// Count of counters
};

/*-----------------------------------------------------------------------------
  Type definitions for analog output
  ----------------------------------------------------------------------------*/

struct me4000_ao_channel_list {
	unsigned long count;
	unsigned long *list;
};

/*-----------------------------------------------------------------------------
  Type definitions for analog input
  ----------------------------------------------------------------------------*/

struct me4000_ai_channel_list {
	unsigned long count;
	unsigned long *list;
};

struct me4000_ai_timer {
	unsigned long pre_chan;
	unsigned long chan;
	unsigned long scan_low;
	unsigned long scan_high;
};

struct me4000_ai_config {
	struct me4000_ai_timer timer;
	struct me4000_ai_channel_list channel_list;
	int sh;
};

struct me4000_ai_single {
	int channel;
	int range;
	int mode;
	short value;
	unsigned long timeout;
};

struct me4000_ai_trigger {
	int mode;
	int edge;
};

struct me4000_ai_sc {
	unsigned long value;
	int reload;
};

/*-----------------------------------------------------------------------------
  Type definitions for eeprom
  ----------------------------------------------------------------------------*/

struct me4000_eeprom {
	unsigned long date;
	short uni_10_offset;
	short uni_10_fullscale;
	short uni_2_5_offset;
	short uni_2_5_fullscale;
	short bi_10_offset;
	short bi_10_fullscale;
	short bi_2_5_offset;
	short bi_2_5_fullscale;
	short diff_10_offset;
	short diff_10_fullscale;
	short diff_2_5_offset;
	short diff_2_5_fullscale;
};

/*-----------------------------------------------------------------------------
  Type definitions for digital I/O
  ----------------------------------------------------------------------------*/

struct me4000_dio_config {
	int port;
	int mode;
	int function;
};

struct me4000_dio_byte {
	int port;
	unsigned char byte;
};

/*-----------------------------------------------------------------------------
  Type definitions for counters
  ----------------------------------------------------------------------------*/

struct me4000_cnt {
	int counter;
	unsigned short value;
};

struct me4000_cnt_config {
	int counter;
	int mode;
};

/*-----------------------------------------------------------------------------
  Type definitions for external interrupt
  ----------------------------------------------------------------------------*/

struct me4000_int {
	int int1_count;
	int int2_count;
};

/*-----------------------------------------------------------------------------
  The ioctls of the board
  ----------------------------------------------------------------------------*/

#define ME4000_IOCTL_MAXNR 50
#define ME4000_MAGIC 'y'
#define ME4000_GET_USER_INFO          _IOR (ME4000_MAGIC, 0, \
					    struct me4000_user_info)

#define ME4000_AO_START               _IOW (ME4000_MAGIC, 1, unsigned long)
#define ME4000_AO_STOP                _IO  (ME4000_MAGIC, 2)
#define ME4000_AO_IMMEDIATE_STOP      _IO  (ME4000_MAGIC, 3)
#define ME4000_AO_RESET               _IO  (ME4000_MAGIC, 4)
#define ME4000_AO_PRELOAD             _IO  (ME4000_MAGIC, 5)
#define ME4000_AO_PRELOAD_UPDATE      _IO  (ME4000_MAGIC, 6)
#define ME4000_AO_EX_TRIG_ENABLE      _IO  (ME4000_MAGIC, 7)
#define ME4000_AO_EX_TRIG_DISABLE     _IO  (ME4000_MAGIC, 8)
#define ME4000_AO_EX_TRIG_SETUP       _IOW (ME4000_MAGIC, 9, int)
#define ME4000_AO_TIMER_SET_DIVISOR   _IOW (ME4000_MAGIC, 10, unsigned long)
#define ME4000_AO_ENABLE_DO           _IO  (ME4000_MAGIC, 11)
#define ME4000_AO_DISABLE_DO          _IO  (ME4000_MAGIC, 12)
#define ME4000_AO_FSM_STATE           _IOR (ME4000_MAGIC, 13, int)

#define ME4000_AI_SINGLE              _IOR (ME4000_MAGIC, 14, \
					    struct me4000_ai_single)
#define ME4000_AI_START               _IOW (ME4000_MAGIC, 15, unsigned long)
#define ME4000_AI_STOP                _IO  (ME4000_MAGIC, 16)
#define ME4000_AI_IMMEDIATE_STOP      _IO  (ME4000_MAGIC, 17)
#define ME4000_AI_EX_TRIG_ENABLE      _IO  (ME4000_MAGIC, 18)
#define ME4000_AI_EX_TRIG_DISABLE     _IO  (ME4000_MAGIC, 19)
#define ME4000_AI_EX_TRIG_SETUP       _IOW (ME4000_MAGIC, 20, \
					    struct me4000_ai_trigger)
#define ME4000_AI_CONFIG              _IOW (ME4000_MAGIC, 21, \
					    struct me4000_ai_config)
#define ME4000_AI_SC_SETUP            _IOW (ME4000_MAGIC, 22, \
					    struct me4000_ai_sc)
#define ME4000_AI_FSM_STATE           _IOR (ME4000_MAGIC, 23, int)

#define ME4000_DIO_CONFIG             _IOW (ME4000_MAGIC, 24, \
					    struct me4000_dio_config)
#define ME4000_DIO_GET_BYTE           _IOR (ME4000_MAGIC, 25, \
					    struct me4000_dio_byte)
#define ME4000_DIO_SET_BYTE           _IOW (ME4000_MAGIC, 26, \
					    struct me4000_dio_byte)
#define ME4000_DIO_RESET              _IO  (ME4000_MAGIC, 27)

#define ME4000_CNT_READ               _IOR (ME4000_MAGIC, 28, \
					    struct me4000_cnt)
#define ME4000_CNT_WRITE              _IOW (ME4000_MAGIC, 29, \
					    struct me4000_cnt)
#define ME4000_CNT_CONFIG             _IOW (ME4000_MAGIC, 30, \
					    struct me4000_cnt_config)
#define ME4000_CNT_RESET              _IO  (ME4000_MAGIC, 31)

#define ME4000_EXT_INT_DISABLE        _IO  (ME4000_MAGIC, 32)
#define ME4000_EXT_INT_ENABLE         _IO  (ME4000_MAGIC, 33)
#define ME4000_EXT_INT_COUNT          _IOR (ME4000_MAGIC, 34, int)

#define ME4000_AI_OFFSET_ENABLE       _IO  (ME4000_MAGIC, 35)
#define ME4000_AI_OFFSET_DISABLE      _IO  (ME4000_MAGIC, 36)
#define ME4000_AI_FULLSCALE_ENABLE    _IO  (ME4000_MAGIC, 37)
#define ME4000_AI_FULLSCALE_DISABLE   _IO  (ME4000_MAGIC, 38)

#define ME4000_AI_EEPROM_READ         _IOR (ME4000_MAGIC, 39, \
					    struct me4000_eeprom)
#define ME4000_AI_EEPROM_WRITE        _IOW (ME4000_MAGIC, 40, \
					    struct me4000_eeprom)

#define ME4000_AO_SIMULTANEOUS_EX_TRIG _IO  (ME4000_MAGIC, 41)
#define ME4000_AO_SIMULTANEOUS_SW      _IO  (ME4000_MAGIC, 42)
#define ME4000_AO_SIMULTANEOUS_DISABLE _IO  (ME4000_MAGIC, 43)
#define ME4000_AO_SIMULTANEOUS_UPDATE  _IOW (ME4000_MAGIC, 44, \
					     struct me4000_ao_channel_list)

#define ME4000_AO_SYNCHRONOUS_EX_TRIG  _IO  (ME4000_MAGIC, 45)
#define ME4000_AO_SYNCHRONOUS_SW       _IO  (ME4000_MAGIC, 46)
#define ME4000_AO_SYNCHRONOUS_DISABLE  _IO  (ME4000_MAGIC, 47)

#define ME4000_AO_EX_TRIG_TIMEOUT      _IOW (ME4000_MAGIC, 48, unsigned long)
#define ME4000_AO_GET_FREE_BUFFER      _IOR (ME4000_MAGIC, 49, unsigned long)

#define ME4000_AI_GET_COUNT_BUFFER     _IOR (ME4000_MAGIC, 50, unsigned long)

#endif
