// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Microsoft Corporation.
 *
 * Author:
 *   Jake Oshins <jakeo@microsoft.com>
 *
 * This driver acts as a paravirtual front-end for PCI Express root buses.
 * When a PCI Express function (either an entire device or an SR-IOV
 * Virtual Function) is being passed through to the VM, this driver exposes
 * a new bus to the guest VM.  This is modeled as a root PCI bus because
 * no bridges are being exposed to the VM.  In fact, with a "Generation 2"
 * VM within Hyper-V, there may seem to be no PCI bus at all in the VM
 * until a device as been exposed using this driver.
 *
 * Each root PCI bus has its own PCI domain, which is called "Segment" in
 * the PCI Firmware Specifications.  Thus while each device passed through
 * to the VM using this front-end will appear at "device 0", the domain will
 * be unique.  Typically, each bus will have one PCI function on it, though
 * this driver does support more than one.
 *
 * In order to map the interrupts from the device through to the guest VM,
 * this driver also implements an IRQ Domain, which handles interrupts (either
 * MSI or MSI-X) associated with the functions on the bus.  As interrupts are
 * set up, torn down, or reaffined, this driver communicates with the
 * underlying hypervisor to adjust the mappings in the I/O MMU so that each
 * interrupt will be delivered to the correct virtual processor at the right
 * vector.  This driver does not support level-triggered (line-based)
 * interrupts, and will report that the Interrupt Line register in the
 * function's configuration space is zero.
 *
 * The rest of this driver mostly maps PCI concepts onto underlying Hyper-V
 * facilities.  For instance, the configuration space of a function exposed
 * by Hyper-V is mapped into a single page of memory space, and the
 * read and write handlers for config space must be aware of this mechanism.
 * Similarly, device setup and teardown involves messages sent to and from
 * the PCI back-end driver in Hyper-V.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/irqdomain.h>
#include <asm/irqdomain.h>
#include <asm/apic.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include <linux/hyperv.h>
#include <linux/refcount.h>
#include <asm/mshyperv.h>

/*
 * Protocol versions. The low word is the minor version, the high word the
 * major version.
 */

#define PCI_MAKE_VERSION(major, minor) ((u32)(((major) << 16) | (minor)))
#define PCI_MAJOR_VERSION(version) ((u32)(version) >> 16)
#define PCI_MINOR_VERSION(version) ((u32)(version) & 0xff)

enum pci_protocol_version_t {
	PCI_PROTOCOL_VERSION_1_1 = PCI_MAKE_VERSION(1, 1),	/* Win10 */
	PCI_PROTOCOL_VERSION_1_2 = PCI_MAKE_VERSION(1, 2),	/* RS1 */
	PCI_PROTOCOL_VERSION_1_3 = PCI_MAKE_VERSION(1, 3),	/* Vibranium */
};

#define CPU_AFFINITY_ALL	-1ULL

/*
 * Supported protocol versions in the order of probing - highest go
 * first.
 */
static enum pci_protocol_version_t pci_protocol_versions[] = {
	PCI_PROTOCOL_VERSION_1_3,
	PCI_PROTOCOL_VERSION_1_2,
	PCI_PROTOCOL_VERSION_1_1,
};

#define PCI_CONFIG_MMIO_LENGTH	0x2000
#define CFG_PAGE_OFFSET 0x1000
#define CFG_PAGE_SIZE (PCI_CONFIG_MMIO_LENGTH - CFG_PAGE_OFFSET)

#define MAX_SUPPORTED_MSI_MESSAGES 0x400

#define STATUS_REVISION_MISMATCH 0xC0000059

/* space for 32bit serial number as string */
#define SLOT_NAME_SIZE 11

/*
 * Message Types
 */

enum pci_message_type {
	/*
	 * Version 1.1
	 */
	PCI_MESSAGE_BASE                = 0x42490000,
	PCI_BUS_RELATIONS               = PCI_MESSAGE_BASE + 0,
	PCI_QUERY_BUS_RELATIONS         = PCI_MESSAGE_BASE + 1,
	PCI_POWER_STATE_CHANGE          = PCI_MESSAGE_BASE + 4,
	PCI_QUERY_RESOURCE_REQUIREMENTS = PCI_MESSAGE_BASE + 5,
	PCI_QUERY_RESOURCE_RESOURCES    = PCI_MESSAGE_BASE + 6,
	PCI_BUS_D0ENTRY                 = PCI_MESSAGE_BASE + 7,
	PCI_BUS_D0EXIT                  = PCI_MESSAGE_BASE + 8,
	PCI_READ_BLOCK                  = PCI_MESSAGE_BASE + 9,
	PCI_WRITE_BLOCK                 = PCI_MESSAGE_BASE + 0xA,
	PCI_EJECT                       = PCI_MESSAGE_BASE + 0xB,
	PCI_QUERY_STOP                  = PCI_MESSAGE_BASE + 0xC,
	PCI_REENABLE                    = PCI_MESSAGE_BASE + 0xD,
	PCI_QUERY_STOP_FAILED           = PCI_MESSAGE_BASE + 0xE,
	PCI_EJECTION_COMPLETE           = PCI_MESSAGE_BASE + 0xF,
	PCI_RESOURCES_ASSIGNED          = PCI_MESSAGE_BASE + 0x10,
	PCI_RESOURCES_RELEASED          = PCI_MESSAGE_BASE + 0x11,
	PCI_INVALIDATE_BLOCK            = PCI_MESSAGE_BASE + 0x12,
	PCI_QUERY_PROTOCOL_VERSION      = PCI_MESSAGE_BASE + 0x13,
	PCI_CREATE_INTERRUPT_MESSAGE    = PCI_MESSAGE_BASE + 0x14,
	PCI_DELETE_INTERRUPT_MESSAGE    = PCI_MESSAGE_BASE + 0x15,
	PCI_RESOURCES_ASSIGNED2		= PCI_MESSAGE_BASE + 0x16,
	PCI_CREATE_INTERRUPT_MESSAGE2	= PCI_MESSAGE_BASE + 0x17,
	PCI_DELETE_INTERRUPT_MESSAGE2	= PCI_MESSAGE_BASE + 0x18, /* unused */
	PCI_BUS_RELATIONS2		= PCI_MESSAGE_BASE + 0x19,
	PCI_MESSAGE_MAXIMUM
};

/*
 * Structures defining the virtual PCI Express protocol.
 */

union pci_version {
	struct {
		u16 minor_version;
		u16 major_version;
	} parts;
	u32 version;
} __packed;

/*
 * Function numbers are 8-bits wide on Express, as interpreted through ARI,
 * which is all this driver does.  This representation is the one used in
 * Windows, which is what is expected when sending this back and forth with
 * the Hyper-V parent partition.
 */
union win_slot_encoding {
	struct {
		u32	dev:5;
		u32	func:3;
		u32	reserved:24;
	} bits;
	u32 slot;
} __packed;

/*
 * Pretty much as defined in the PCI Specifications.
 */
struct pci_function_description {
	u16	v_id;	/* vendor ID */
	u16	d_id;	/* device ID */
	u8	rev;
	u8	prog_intf;
	u8	subclass;
	u8	base_class;
	u32	subsystem_id;
	union win_slot_encoding win_slot;
	u32	ser;	/* serial number */
} __packed;

enum pci_device_description_flags {
	HV_PCI_DEVICE_FLAG_NONE			= 0x0,
	HV_PCI_DEVICE_FLAG_NUMA_AFFINITY	= 0x1,
};

struct pci_function_description2 {
	u16	v_id;	/* vendor ID */
	u16	d_id;	/* device ID */
	u8	rev;
	u8	prog_intf;
	u8	subclass;
	u8	base_class;
	u32	subsystem_id;
	union	win_slot_encoding win_slot;
	u32	ser;	/* serial number */
	u32	flags;
	u16	virtual_numa_node;
	u16	reserved;
} __packed;

/**
 * struct hv_msi_desc
 * @vector:		IDT entry
 * @delivery_mode:	As defined in Intel's Programmer's
 *			Reference Manual, Volume 3, Chapter 8.
 * @vector_count:	Number of contiguous entries in the
 *			Interrupt Descriptor Table that are
 *			occupied by this Message-Signaled
 *			Interrupt. For "MSI", as first defined
 *			in PCI 2.2, this can be between 1 and
 *			32. For "MSI-X," as first defined in PCI
 *			3.0, this must be 1, as each MSI-X table
 *			entry would have its own descriptor.
 * @reserved:		Empty space
 * @cpu_mask:		All the target virtual processors.
 */
struct hv_msi_desc {
	u8	vector;
	u8	delivery_mode;
	u16	vector_count;
	u32	reserved;
	u64	cpu_mask;
} __packed;

/**
 * struct hv_msi_desc2 - 1.2 version of hv_msi_desc
 * @vector:		IDT entry
 * @delivery_mode:	As defined in Intel's Programmer's
 *			Reference Manual, Volume 3, Chapter 8.
 * @vector_count:	Number of contiguous entries in the
 *			Interrupt Descriptor Table that are
 *			occupied by this Message-Signaled
 *			Interrupt. For "MSI", as first defined
 *			in PCI 2.2, this can be between 1 and
 *			32. For "MSI-X," as first defined in PCI
 *			3.0, this must be 1, as each MSI-X table
 *			entry would have its own descriptor.
 * @processor_count:	number of bits enabled in array.
 * @processor_array:	All the target virtual processors.
 */
struct hv_msi_desc2 {
	u8	vector;
	u8	delivery_mode;
	u16	vector_count;
	u16	processor_count;
	u16	processor_array[32];
} __packed;

/**
 * struct tran_int_desc
 * @reserved:		unused, padding
 * @vector_count:	same as in hv_msi_desc
 * @data:		This is the "data payload" value that is
 *			written by the device when it generates
 *			a message-signaled interrupt, either MSI
 *			or MSI-X.
 * @address:		This is the address to which the data
 *			payload is written on interrupt
 *			generation.
 */
struct tran_int_desc {
	u16	reserved;
	u16	vector_count;
	u32	data;
	u64	address;
} __packed;

/*
 * A generic message format for virtual PCI.
 * Specific message formats are defined later in the file.
 */

struct pci_message {
	u32 type;
} __packed;

struct pci_child_message {
	struct pci_message message_type;
	union win_slot_encoding wslot;
} __packed;

struct pci_incoming_message {
	struct vmpacket_descriptor hdr;
	struct pci_message message_type;
} __packed;

struct pci_response {
	struct vmpacket_descriptor hdr;
	s32 status;			/* negative values are failures */
} __packed;

struct pci_packet {
	void (*completion_func)(void *context, struct pci_response *resp,
				int resp_packet_size);
	void *compl_ctxt;

	struct pci_message message[];
};

/*
 * Specific message types supporting the PCI protocol.
 */

/*
 * Version negotiation message. Sent from the guest to the host.
 * The guest is free to try different versions until the host
 * accepts the version.
 *
 * pci_version: The protocol version requested.
 * is_last_attempt: If TRUE, this is the last version guest will request.
 * reservedz: Reserved field, set to zero.
 */

struct pci_version_request {
	struct pci_message message_type;
	u32 protocol_version;
} __packed;

/*
 * Bus D0 Entry.  This is sent from the guest to the host when the virtual
 * bus (PCI Express port) is ready for action.
 */

struct pci_bus_d0_entry {
	struct pci_message message_type;
	u32 reserved;
	u64 mmio_base;
} __packed;

struct pci_bus_relations {
	struct pci_incoming_message incoming;
	u32 device_count;
	struct pci_function_description func[];
} __packed;

struct pci_bus_relations2 {
	struct pci_incoming_message incoming;
	u32 device_count;
	struct pci_function_description2 func[];
} __packed;

struct pci_q_res_req_response {
	struct vmpacket_descriptor hdr;
	s32 status;			/* negative values are failures */
	u32 probed_bar[PCI_STD_NUM_BARS];
} __packed;

struct pci_set_power {
	struct pci_message message_type;
	union win_slot_encoding wslot;
	u32 power_state;		/* In Windows terms */
	u32 reserved;
} __packed;

struct pci_set_power_response {
	struct vmpacket_descriptor hdr;
	s32 status;			/* negative values are failures */
	union win_slot_encoding wslot;
	u32 resultant_state;		/* In Windows terms */
	u32 reserved;
} __packed;

struct pci_resources_assigned {
	struct pci_message message_type;
	union win_slot_encoding wslot;
	u8 memory_range[0x14][6];	/* not used here */
	u32 msi_descriptors;
	u32 reserved[4];
} __packed;

struct pci_resources_assigned2 {
	struct pci_message message_type;
	union win_slot_encoding wslot;
	u8 memory_range[0x14][6];	/* not used here */
	u32 msi_descriptor_count;
	u8 reserved[70];
} __packed;

struct pci_create_interrupt {
	struct pci_message message_type;
	union win_slot_encoding wslot;
	struct hv_msi_desc int_desc;
} __packed;

struct pci_create_int_response {
	struct pci_response response;
	u32 reserved;
	struct tran_int_desc int_desc;
} __packed;

struct pci_create_interrupt2 {
	struct pci_message message_type;
	union win_slot_encoding wslot;
	struct hv_msi_desc2 int_desc;
} __packed;

struct pci_delete_interrupt {
	struct pci_message message_type;
	union win_slot_encoding wslot;
	struct tran_int_desc int_desc;
} __packed;

/*
 * Note: the VM must pass a valid block id, wslot and bytes_requested.
 */
struct pci_read_block {
	struct pci_message message_type;
	u32 block_id;
	union win_slot_encoding wslot;
	u32 bytes_requested;
} __packed;

struct pci_read_block_response {
	struct vmpacket_descriptor hdr;
	u32 status;
	u8 bytes[HV_CONFIG_BLOCK_SIZE_MAX];
} __packed;

/*
 * Note: the VM must pass a valid block id, wslot and byte_count.
 */
struct pci_write_block {
	struct pci_message message_type;
	u32 block_id;
	union win_slot_encoding wslot;
	u32 byte_count;
	u8 bytes[HV_CONFIG_BLOCK_SIZE_MAX];
} __packed;

struct pci_dev_inval_block {
	struct pci_incoming_message incoming;
	union win_slot_encoding wslot;
	u64 block_mask;
} __packed;

struct pci_dev_incoming {
	struct pci_incoming_message incoming;
	union win_slot_encoding wslot;
} __packed;

struct pci_eject_response {
	struct pci_message message_type;
	union win_slot_encoding wslot;
	u32 status;
} __packed;

static int pci_ring_size = (4 * PAGE_SIZE);

/*
 * Driver specific state.
 */

enum hv_pcibus_state {
	hv_pcibus_init = 0,
	hv_pcibus_probed,
	hv_pcibus_installed,
	hv_pcibus_removing,
	hv_pcibus_maximum
};

struct hv_pcibus_device {
	struct pci_sysdata sysdata;
	/* Protocol version negotiated with the host */
	enum pci_protocol_version_t protocol_version;
	enum hv_pcibus_state state;
	refcount_t remove_lock;
	struct hv_device *hdev;
	resource_size_t low_mmio_space;
	resource_size_t high_mmio_space;
	struct resource *mem_config;
	struct resource *low_mmio_res;
	struct resource *high_mmio_res;
	struct completion *survey_event;
	struct completion remove_event;
	struct pci_bus *pci_bus;
	spinlock_t config_lock;	/* Avoid two threads writing index page */
	spinlock_t device_list_lock;	/* Protect lists below */
	void __iomem *cfg_addr;

	struct list_head resources_for_children;

	struct list_head children;
	struct list_head dr_list;

	struct msi_domain_info msi_info;
	struct msi_controller msi_chip;
	struct irq_domain *irq_domain;

	spinlock_t retarget_msi_interrupt_lock;

	struct workqueue_struct *wq;

	/* Highest slot of child device with resources allocated */
	int wslot_res_allocated;

	/* hypercall arg, must not cross page boundary */
	struct hv_retarget_device_interrupt retarget_msi_interrupt_params;

	/*
	 * Don't put anything here: retarget_msi_interrupt_params must be last
	 */
};

/*
 * Tracks "Device Relations" messages from the host, which must be both
 * processed in order and deferred so that they don't run in the context
 * of the incoming packet callback.
 */
struct hv_dr_work {
	struct work_struct wrk;
	struct hv_pcibus_device *bus;
};

struct hv_pcidev_description {
	u16	v_id;	/* vendor ID */
	u16	d_id;	/* device ID */
	u8	rev;
	u8	prog_intf;
	u8	subclass;
	u8	base_class;
	u32	subsystem_id;
	union	win_slot_encoding win_slot;
	u32	ser;	/* serial number */
	u32	flags;
	u16	virtual_numa_node;
};

struct hv_dr_state {
	struct list_head list_entry;
	u32 device_count;
	struct hv_pcidev_description func[];
};

enum hv_pcichild_state {
	hv_pcichild_init = 0,
	hv_pcichild_requirements,
	hv_pcichild_resourced,
	hv_pcichild_ejecting,
	hv_pcichild_maximum
};

struct hv_pci_dev {
	/* List protected by pci_rescan_remove_lock */
	struct list_head list_entry;
	refcount_t refs;
	enum hv_pcichild_state state;
	struct pci_slot *pci_slot;
	struct hv_pcidev_description desc;
	bool reported_missing;
	struct hv_pcibus_device *hbus;
	struct work_struct wrk;

	void (*block_invalidate)(void *context, u64 block_mask);
	void *invalidate_context;

	/*
	 * What would be observed if one wrote 0xFFFFFFFF to a BAR and then
	 * read it back, for each of the BAR offsets within config space.
	 */
	u32 probed_bar[PCI_STD_NUM_BARS];
};

struct hv_pci_compl {
	struct completion host_event;
	s32 completion_status;
};

static void hv_pci_onchannelcallback(void *context);

/**
 * hv_pci_generic_compl() - Invoked for a completion packet
 * @context:		Set up by the sender of the packet.
 * @resp:		The response packet
 * @resp_packet_size:	Size in bytes of the packet
 *
 * This function is used to trigger an event and report status
 * for any message for which the completion packet contains a
 * status and nothing else.
 */
static void hv_pci_generic_compl(void *context, struct pci_response *resp,
				 int resp_packet_size)
{
	struct hv_pci_compl *comp_pkt = context;

	if (resp_packet_size >= offsetofend(struct pci_response, status))
		comp_pkt->completion_status = resp->status;
	else
		comp_pkt->completion_status = -1;

	complete(&comp_pkt->host_event);
}

static struct hv_pci_dev *get_pcichild_wslot(struct hv_pcibus_device *hbus,
						u32 wslot);

static void get_pcichild(struct hv_pci_dev *hpdev)
{
	refcount_inc(&hpdev->refs);
}

static void put_pcichild(struct hv_pci_dev *hpdev)
{
	if (refcount_dec_and_test(&hpdev->refs))
		kfree(hpdev);
}

static void get_hvpcibus(struct hv_pcibus_device *hv_pcibus);
static void put_hvpcibus(struct hv_pcibus_device *hv_pcibus);

/*
 * There is no good way to get notified from vmbus_onoffer_rescind(),
 * so let's use polling here, since this is not a hot path.
 */
static int wait_for_response(struct hv_device *hdev,
			     struct completion *comp)
{
	while (true) {
		if (hdev->channel->rescind) {
			dev_warn_once(&hdev->device, "The device is gone.\n");
			return -ENODEV;
		}

		if (wait_for_completion_timeout(comp, HZ / 10))
			break;
	}

	return 0;
}

/**
 * devfn_to_wslot() - Convert from Linux PCI slot to Windows
 * @devfn:	The Linux representation of PCI slot
 *
 * Windows uses a slightly different representation of PCI slot.
 *
 * Return: The Windows representation
 */
static u32 devfn_to_wslot(int devfn)
{
	union win_slot_encoding wslot;

	wslot.slot = 0;
	wslot.bits.dev = PCI_SLOT(devfn);
	wslot.bits.func = PCI_FUNC(devfn);

	return wslot.slot;
}

/**
 * wslot_to_devfn() - Convert from Windows PCI slot to Linux
 * @wslot:	The Windows representation of PCI slot
 *
 * Windows uses a slightly different representation of PCI slot.
 *
 * Return: The Linux representation
 */
static int wslot_to_devfn(u32 wslot)
{
	union win_slot_encoding slot_no;

	slot_no.slot = wslot;
	return PCI_DEVFN(slot_no.bits.dev, slot_no.bits.func);
}

/*
 * PCI Configuration Space for these root PCI buses is implemented as a pair
 * of pages in memory-mapped I/O space.  Writing to the first page chooses
 * the PCI function being written or read.  Once the first page has been
 * written to, the following page maps in the entire configuration space of
 * the function.
 */

/**
 * _hv_pcifront_read_config() - Internal PCI config read
 * @hpdev:	The PCI driver's representation of the device
 * @where:	Offset within config space
 * @size:	Size of the transfer
 * @val:	Pointer to the buffer receiving the data
 */
static void _hv_pcifront_read_config(struct hv_pci_dev *hpdev, int where,
				     int size, u32 *val)
{
	unsigned long flags;
	void __iomem *addr = hpdev->hbus->cfg_addr + CFG_PAGE_OFFSET + where;

	/*
	 * If the attempt is to read the IDs or the ROM BAR, simulate that.
	 */
	if (where + size <= PCI_COMMAND) {
		memcpy(val, ((u8 *)&hpdev->desc.v_id) + where, size);
	} else if (where >= PCI_CLASS_REVISION && where + size <=
		   PCI_CACHE_LINE_SIZE) {
		memcpy(val, ((u8 *)&hpdev->desc.rev) + where -
		       PCI_CLASS_REVISION, size);
	} else if (where >= PCI_SUBSYSTEM_VENDOR_ID && where + size <=
		   PCI_ROM_ADDRESS) {
		memcpy(val, (u8 *)&hpdev->desc.subsystem_id + where -
		       PCI_SUBSYSTEM_VENDOR_ID, size);
	} else if (where >= PCI_ROM_ADDRESS && where + size <=
		   PCI_CAPABILITY_LIST) {
		/* ROM BARs are unimplemented */
		*val = 0;
	} else if (where >= PCI_INTERRUPT_LINE && where + size <=
		   PCI_INTERRUPT_PIN) {
		/*
		 * Interrupt Line and Interrupt PIN are hard-wired to zero
		 * because this front-end only supports message-signaled
		 * interrupts.
		 */
		*val = 0;
	} else if (where + size <= CFG_PAGE_SIZE) {
		spin_lock_irqsave(&hpdev->hbus->config_lock, flags);
		/* Choose the function to be read. (See comment above) */
		writel(hpdev->desc.win_slot.slot, hpdev->hbus->cfg_addr);
		/* Make sure the function was chosen before we start reading. */
		mb();
		/* Read from that function's config space. */
		switch (size) {
		case 1:
			*val = readb(addr);
			break;
		case 2:
			*val = readw(addr);
			break;
		default:
			*val = readl(addr);
			break;
		}
		/*
		 * Make sure the read was done before we release the spinlock
		 * allowing consecutive reads/writes.
		 */
		mb();
		spin_unlock_irqrestore(&hpdev->hbus->config_lock, flags);
	} else {
		dev_err(&hpdev->hbus->hdev->device,
			"Attempt to read beyond a function's config space.\n");
	}
}

static u16 hv_pcifront_get_vendor_id(struct hv_pci_dev *hpdev)
{
	u16 ret;
	unsigned long flags;
	void __iomem *addr = hpdev->hbus->cfg_addr + CFG_PAGE_OFFSET +
			     PCI_VENDOR_ID;

	spin_lock_irqsave(&hpdev->hbus->config_lock, flags);

	/* Choose the function to be read. (See comment above) */
	writel(hpdev->desc.win_slot.slot, hpdev->hbus->cfg_addr);
	/* Make sure the function was chosen before we start reading. */
	mb();
	/* Read from that function's config space. */
	ret = readw(addr);
	/*
	 * mb() is not required here, because the spin_unlock_irqrestore()
	 * is a barrier.
	 */

	spin_unlock_irqrestore(&hpdev->hbus->config_lock, flags);

	return ret;
}

/**
 * _hv_pcifront_write_config() - Internal PCI config write
 * @hpdev:	The PCI driver's representation of the device
 * @where:	Offset within config space
 * @size:	Size of the transfer
 * @val:	The data being transferred
 */
static void _hv_pcifront_write_config(struct hv_pci_dev *hpdev, int where,
				      int size, u32 val)
{
	unsigned long flags;
	void __iomem *addr = hpdev->hbus->cfg_addr + CFG_PAGE_OFFSET + where;

	if (where >= PCI_SUBSYSTEM_VENDOR_ID &&
	    where + size <= PCI_CAPABILITY_LIST) {
		/* SSIDs and ROM BARs are read-only */
	} else if (where >= PCI_COMMAND && where + size <= CFG_PAGE_SIZE) {
		spin_lock_irqsave(&hpdev->hbus->config_lock, flags);
		/* Choose the function to be written. (See comment above) */
		writel(hpdev->desc.win_slot.slot, hpdev->hbus->cfg_addr);
		/* Make sure the function was chosen before we start writing. */
		wmb();
		/* Write to that function's config space. */
		switch (size) {
		case 1:
			writeb(val, addr);
			break;
		case 2:
			writew(val, addr);
			break;
		default:
			writel(val, addr);
			break;
		}
		/*
		 * Make sure the write was done before we release the spinlock
		 * allowing consecutive reads/writes.
		 */
		mb();
		spin_unlock_irqrestore(&hpdev->hbus->config_lock, flags);
	} else {
		dev_err(&hpdev->hbus->hdev->device,
			"Attempt to write beyond a function's config space.\n");
	}
}

/**
 * hv_pcifront_read_config() - Read configuration space
 * @bus: PCI Bus structure
 * @devfn: Device/function
 * @where: Offset from base
 * @size: Byte/word/dword
 * @val: Value to be read
 *
 * Return: PCIBIOS_SUCCESSFUL on success
 *	   PCIBIOS_DEVICE_NOT_FOUND on failure
 */
static int hv_pcifront_read_config(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 *val)
{
	struct hv_pcibus_device *hbus =
		container_of(bus->sysdata, struct hv_pcibus_device, sysdata);
	struct hv_pci_dev *hpdev;

	hpdev = get_pcichild_wslot(hbus, devfn_to_wslot(devfn));
	if (!hpdev)
		return PCIBIOS_DEVICE_NOT_FOUND;

	_hv_pcifront_read_config(hpdev, where, size, val);

	put_pcichild(hpdev);
	return PCIBIOS_SUCCESSFUL;
}

/**
 * hv_pcifront_write_config() - Write configuration space
 * @bus: PCI Bus structure
 * @devfn: Device/function
 * @where: Offset from base
 * @size: Byte/word/dword
 * @val: Value to be written to device
 *
 * Return: PCIBIOS_SUCCESSFUL on success
 *	   PCIBIOS_DEVICE_NOT_FOUND on failure
 */
static int hv_pcifront_write_config(struct pci_bus *bus, unsigned int devfn,
				    int where, int size, u32 val)
{
	struct hv_pcibus_device *hbus =
	    container_of(bus->sysdata, struct hv_pcibus_device, sysdata);
	struct hv_pci_dev *hpdev;

	hpdev = get_pcichild_wslot(hbus, devfn_to_wslot(devfn));
	if (!hpdev)
		return PCIBIOS_DEVICE_NOT_FOUND;

	_hv_pcifront_write_config(hpdev, where, size, val);

	put_pcichild(hpdev);
	return PCIBIOS_SUCCESSFUL;
}

/* PCIe operations */
static struct pci_ops hv_pcifront_ops = {
	.read  = hv_pcifront_read_config,
	.write = hv_pcifront_write_config,
};

/*
 * Paravirtual backchannel
 *
 * Hyper-V SR-IOV provides a backchannel mechanism in software for
 * communication between a VF driver and a PF driver.  These
 * "configuration blocks" are similar in concept to PCI configuration space,
 * but instead of doing reads and writes in 32-bit chunks through a very slow
 * path, packets of up to 128 bytes can be sent or received asynchronously.
 *
 * Nearly every SR-IOV device contains just such a communications channel in
 * hardware, so using this one in software is usually optional.  Using the
 * software channel, however, allows driver implementers to leverage software
 * tools that fuzz the communications channel looking for vulnerabilities.
 *
 * The usage model for these packets puts the responsibility for reading or
 * writing on the VF driver.  The VF driver sends a read or a write packet,
 * indicating which "block" is being referred to by number.
 *
 * If the PF driver wishes to initiate communication, it can "invalidate" one or
 * more of the first 64 blocks.  This invalidation is delivered via a callback
 * supplied by the VF driver by this driver.
 *
 * No protocol is implied, except that supplied by the PF and VF drivers.
 */

struct hv_read_config_compl {
	struct hv_pci_compl comp_pkt;
	void *buf;
	unsigned int len;
	unsigned int bytes_returned;
};

/**
 * hv_pci_read_config_compl() - Invoked when a response packet
 * for a read config block operation arrives.
 * @context:		Identifies the read config operation
 * @resp:		The response packet itself
 * @resp_packet_size:	Size in bytes of the response packet
 */
static void hv_pci_read_config_compl(void *context, struct pci_response *resp,
				     int resp_packet_size)
{
	struct hv_read_config_compl *comp = context;
	struct pci_read_block_response *read_resp =
		(struct pci_read_block_response *)resp;
	unsigned int data_len, hdr_len;

	hdr_len = offsetof(struct pci_read_block_response, bytes);
	if (resp_packet_size < hdr_len) {
		comp->comp_pkt.completion_status = -1;
		goto out;
	}

	data_len = resp_packet_size - hdr_len;
	if (data_len > 0 && read_resp->status == 0) {
		comp->bytes_returned = min(comp->len, data_len);
		memcpy(comp->buf, read_resp->bytes, comp->bytes_returned);
	} else {
		comp->bytes_returned = 0;
	}

	comp->comp_pkt.completion_status = read_resp->status;
out:
	complete(&comp->comp_pkt.host_event);
}

/**
 * hv_read_config_block() - Sends a read config block request to
 * the back-end driver running in the Hyper-V parent partition.
 * @pdev:		The PCI driver's representation for this device.
 * @buf:		Buffer into which the config block will be copied.
 * @len:		Size in bytes of buf.
 * @block_id:		Identifies the config block which has been requested.
 * @bytes_returned:	Size which came back from the back-end driver.
 *
 * Return: 0 on success, -errno on failure
 */
static int hv_read_config_block(struct pci_dev *pdev, void *buf,
				unsigned int len, unsigned int block_id,
				unsigned int *bytes_returned)
{
	struct hv_pcibus_device *hbus =
		container_of(pdev->bus->sysdata, struct hv_pcibus_device,
			     sysdata);
	struct {
		struct pci_packet pkt;
		char buf[sizeof(struct pci_read_block)];
	} pkt;
	struct hv_read_config_compl comp_pkt;
	struct pci_read_block *read_blk;
	int ret;

	if (len == 0 || len > HV_CONFIG_BLOCK_SIZE_MAX)
		return -EINVAL;

	init_completion(&comp_pkt.comp_pkt.host_event);
	comp_pkt.buf = buf;
	comp_pkt.len = len;

	memset(&pkt, 0, sizeof(pkt));
	pkt.pkt.completion_func = hv_pci_read_config_compl;
	pkt.pkt.compl_ctxt = &comp_pkt;
	read_blk = (struct pci_read_block *)&pkt.pkt.message;
	read_blk->message_type.type = PCI_READ_BLOCK;
	read_blk->wslot.slot = devfn_to_wslot(pdev->devfn);
	read_blk->block_id = block_id;
	read_blk->bytes_requested = len;

	ret = vmbus_sendpacket(hbus->hdev->channel, read_blk,
			       sizeof(*read_blk), (unsigned long)&pkt.pkt,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret)
		return ret;

	ret = wait_for_response(hbus->hdev, &comp_pkt.comp_pkt.host_event);
	if (ret)
		return ret;

	if (comp_pkt.comp_pkt.completion_status != 0 ||
	    comp_pkt.bytes_returned == 0) {
		dev_err(&hbus->hdev->device,
			"Read Config Block failed: 0x%x, bytes_returned=%d\n",
			comp_pkt.comp_pkt.completion_status,
			comp_pkt.bytes_returned);
		return -EIO;
	}

	*bytes_returned = comp_pkt.bytes_returned;
	return 0;
}

/**
 * hv_pci_write_config_compl() - Invoked when a response packet for a write
 * config block operation arrives.
 * @context:		Identifies the write config operation
 * @resp:		The response packet itself
 * @resp_packet_size:	Size in bytes of the response packet
 */
static void hv_pci_write_config_compl(void *context, struct pci_response *resp,
				      int resp_packet_size)
{
	struct hv_pci_compl *comp_pkt = context;

	comp_pkt->completion_status = resp->status;
	complete(&comp_pkt->host_event);
}

/**
 * hv_write_config_block() - Sends a write config block request to the
 * back-end driver running in the Hyper-V parent partition.
 * @pdev:		The PCI driver's representation for this device.
 * @buf:		Buffer from which the config block will	be copied.
 * @len:		Size in bytes of buf.
 * @block_id:		Identifies the config block which is being written.
 *
 * Return: 0 on success, -errno on failure
 */
static int hv_write_config_block(struct pci_dev *pdev, void *buf,
				unsigned int len, unsigned int block_id)
{
	struct hv_pcibus_device *hbus =
		container_of(pdev->bus->sysdata, struct hv_pcibus_device,
			     sysdata);
	struct {
		struct pci_packet pkt;
		char buf[sizeof(struct pci_write_block)];
		u32 reserved;
	} pkt;
	struct hv_pci_compl comp_pkt;
	struct pci_write_block *write_blk;
	u32 pkt_size;
	int ret;

	if (len == 0 || len > HV_CONFIG_BLOCK_SIZE_MAX)
		return -EINVAL;

	init_completion(&comp_pkt.host_event);

	memset(&pkt, 0, sizeof(pkt));
	pkt.pkt.completion_func = hv_pci_write_config_compl;
	pkt.pkt.compl_ctxt = &comp_pkt;
	write_blk = (struct pci_write_block *)&pkt.pkt.message;
	write_blk->message_type.type = PCI_WRITE_BLOCK;
	write_blk->wslot.slot = devfn_to_wslot(pdev->devfn);
	write_blk->block_id = block_id;
	write_blk->byte_count = len;
	memcpy(write_blk->bytes, buf, len);
	pkt_size = offsetof(struct pci_write_block, bytes) + len;
	/*
	 * This quirk is required on some hosts shipped around 2018, because
	 * these hosts don't check the pkt_size correctly (new hosts have been
	 * fixed since early 2019). The quirk is also safe on very old hosts
	 * and new hosts, because, on them, what really matters is the length
	 * specified in write_blk->byte_count.
	 */
	pkt_size += sizeof(pkt.reserved);

	ret = vmbus_sendpacket(hbus->hdev->channel, write_blk, pkt_size,
			       (unsigned long)&pkt.pkt, VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret)
		return ret;

	ret = wait_for_response(hbus->hdev, &comp_pkt.host_event);
	if (ret)
		return ret;

	if (comp_pkt.completion_status != 0) {
		dev_err(&hbus->hdev->device,
			"Write Config Block failed: 0x%x\n",
			comp_pkt.completion_status);
		return -EIO;
	}

	return 0;
}

/**
 * hv_register_block_invalidate() - Invoked when a config block invalidation
 * arrives from the back-end driver.
 * @pdev:		The PCI driver's representation for this device.
 * @context:		Identifies the device.
 * @block_invalidate:	Identifies all of the blocks being invalidated.
 *
 * Return: 0 on success, -errno on failure
 */
static int hv_register_block_invalidate(struct pci_dev *pdev, void *context,
					void (*block_invalidate)(void *context,
								 u64 block_mask))
{
	struct hv_pcibus_device *hbus =
		container_of(pdev->bus->sysdata, struct hv_pcibus_device,
			     sysdata);
	struct hv_pci_dev *hpdev;

	hpdev = get_pcichild_wslot(hbus, devfn_to_wslot(pdev->devfn));
	if (!hpdev)
		return -ENODEV;

	hpdev->block_invalidate = block_invalidate;
	hpdev->invalidate_context = context;

	put_pcichild(hpdev);
	return 0;

}

/* Interrupt management hooks */
static void hv_int_desc_free(struct hv_pci_dev *hpdev,
			     struct tran_int_desc *int_desc)
{
	struct pci_delete_interrupt *int_pkt;
	struct {
		struct pci_packet pkt;
		u8 buffer[sizeof(struct pci_delete_interrupt)];
	} ctxt;

	memset(&ctxt, 0, sizeof(ctxt));
	int_pkt = (struct pci_delete_interrupt *)&ctxt.pkt.message;
	int_pkt->message_type.type =
		PCI_DELETE_INTERRUPT_MESSAGE;
	int_pkt->wslot.slot = hpdev->desc.win_slot.slot;
	int_pkt->int_desc = *int_desc;
	vmbus_sendpacket(hpdev->hbus->hdev->channel, int_pkt, sizeof(*int_pkt),
			 (unsigned long)&ctxt.pkt, VM_PKT_DATA_INBAND, 0);
	kfree(int_desc);
}

/**
 * hv_msi_free() - Free the MSI.
 * @domain:	The interrupt domain pointer
 * @info:	Extra MSI-related context
 * @irq:	Identifies the IRQ.
 *
 * The Hyper-V parent partition and hypervisor are tracking the
 * messages that are in use, keeping the interrupt redirection
 * table up to date.  This callback sends a message that frees
 * the IRT entry and related tracking nonsense.
 */
static void hv_msi_free(struct irq_domain *domain, struct msi_domain_info *info,
			unsigned int irq)
{
	struct hv_pcibus_device *hbus;
	struct hv_pci_dev *hpdev;
	struct pci_dev *pdev;
	struct tran_int_desc *int_desc;
	struct irq_data *irq_data = irq_domain_get_irq_data(domain, irq);
	struct msi_desc *msi = irq_data_get_msi_desc(irq_data);

	pdev = msi_desc_to_pci_dev(msi);
	hbus = info->data;
	int_desc = irq_data_get_irq_chip_data(irq_data);
	if (!int_desc)
		return;

	irq_data->chip_data = NULL;
	hpdev = get_pcichild_wslot(hbus, devfn_to_wslot(pdev->devfn));
	if (!hpdev) {
		kfree(int_desc);
		return;
	}

	hv_int_desc_free(hpdev, int_desc);
	put_pcichild(hpdev);
}

static int hv_set_affinity(struct irq_data *data, const struct cpumask *dest,
			   bool force)
{
	struct irq_data *parent = data->parent_data;

	return parent->chip->irq_set_affinity(parent, dest, force);
}

static void hv_irq_mask(struct irq_data *data)
{
	pci_msi_mask_irq(data);
}

static int hv_msi_prepare(struct irq_domain *domain, struct device *dev,
			  int nvec, msi_alloc_info_t *info)
{
	int ret = pci_msi_prepare(domain, dev, nvec, info);

	/*
	 * By using the interrupt remapper in the hypervisor IOMMU, contiguous
	 * CPU vectors is not needed for multi-MSI
	 */
	if (info->type == X86_IRQ_ALLOC_TYPE_PCI_MSI)
		info->flags &= ~X86_IRQ_ALLOC_CONTIGUOUS_VECTORS;

	return ret;
}

/**
 * hv_irq_unmask() - "Unmask" the IRQ by setting its current
 * affinity.
 * @data:	Describes the IRQ
 *
 * Build new a destination for the MSI and make a hypercall to
 * update the Interrupt Redirection Table. "Device Logical ID"
 * is built out of this PCI bus's instance GUID and the function
 * number of the device.
 */
static void hv_irq_unmask(struct irq_data *data)
{
	struct msi_desc *msi_desc = irq_data_get_msi_desc(data);
	struct irq_cfg *cfg = irqd_cfg(data);
	struct hv_retarget_device_interrupt *params;
	struct tran_int_desc *int_desc;
	struct hv_pcibus_device *hbus;
	struct cpumask *dest;
	cpumask_var_t tmp;
	struct pci_bus *pbus;
	struct pci_dev *pdev;
	unsigned long flags;
	u32 var_size = 0;
	int cpu, nr_bank;
	u64 res;

	dest = irq_data_get_effective_affinity_mask(data);
	pdev = msi_desc_to_pci_dev(msi_desc);
	pbus = pdev->bus;
	hbus = container_of(pbus->sysdata, struct hv_pcibus_device, sysdata);
	int_desc = data->chip_data;

	spin_lock_irqsave(&hbus->retarget_msi_interrupt_lock, flags);

	params = &hbus->retarget_msi_interrupt_params;
	memset(params, 0, sizeof(*params));
	params->partition_id = HV_PARTITION_ID_SELF;
	params->int_entry.source = 1; /* MSI(-X) */
	params->int_entry.msi_entry.address = int_desc->address & 0xffffffff;
	params->int_entry.msi_entry.data = int_desc->data;
	params->device_id = (hbus->hdev->dev_instance.b[5] << 24) |
			   (hbus->hdev->dev_instance.b[4] << 16) |
			   (hbus->hdev->dev_instance.b[7] << 8) |
			   (hbus->hdev->dev_instance.b[6] & 0xf8) |
			   PCI_FUNC(pdev->devfn);
	params->int_target.vector = cfg->vector;

	/*
	 * Honoring apic->irq_delivery_mode set to dest_Fixed by
	 * setting the HV_DEVICE_INTERRUPT_TARGET_MULTICAST flag results in a
	 * spurious interrupt storm. Not doing so does not seem to have a
	 * negative effect (yet?).
	 */

	if (hbus->protocol_version >= PCI_PROTOCOL_VERSION_1_2) {
		/*
		 * PCI_PROTOCOL_VERSION_1_2 supports the VP_SET version of the
		 * HVCALL_RETARGET_INTERRUPT hypercall, which also coincides
		 * with >64 VP support.
		 * ms_hyperv.hints & HV_X64_EX_PROCESSOR_MASKS_RECOMMENDED
		 * is not sufficient for this hypercall.
		 */
		params->int_target.flags |=
			HV_DEVICE_INTERRUPT_TARGET_PROCESSOR_SET;

		if (!alloc_cpumask_var(&tmp, GFP_ATOMIC)) {
			res = 1;
			goto exit_unlock;
		}

		cpumask_and(tmp, dest, cpu_online_mask);
		nr_bank = cpumask_to_vpset(&params->int_target.vp_set, tmp);
		free_cpumask_var(tmp);

		if (nr_bank <= 0) {
			res = 1;
			goto exit_unlock;
		}

		/*
		 * var-sized hypercall, var-size starts after vp_mask (thus
		 * vp_set.format does not count, but vp_set.valid_bank_mask
		 * does).
		 */
		var_size = 1 + nr_bank;
	} else {
		for_each_cpu_and(cpu, dest, cpu_online_mask) {
			params->int_target.vp_mask |=
				(1ULL << hv_cpu_number_to_vp_number(cpu));
		}
	}

	res = hv_do_hypercall(HVCALL_RETARGET_INTERRUPT | (var_size << 17),
			      params, NULL);

exit_unlock:
	spin_unlock_irqrestore(&hbus->retarget_msi_interrupt_lock, flags);

	/*
	 * During hibernation, when a CPU is offlined, the kernel tries
	 * to move the interrupt to the remaining CPUs that haven't
	 * been offlined yet. In this case, the below hv_do_hypercall()
	 * always fails since the vmbus channel has been closed:
	 * refer to cpu_disable_common() -> fixup_irqs() ->
	 * irq_migrate_all_off_this_cpu() -> migrate_one_irq().
	 *
	 * Suppress the error message for hibernation because the failure
	 * during hibernation does not matter (at this time all the devices
	 * have been frozen). Note: the correct affinity info is still updated
	 * into the irqdata data structure in migrate_one_irq() ->
	 * irq_do_set_affinity() -> hv_set_affinity(), so later when the VM
	 * resumes, hv_pci_restore_msi_state() is able to correctly restore
	 * the interrupt with the correct affinity.
	 */
	if (res && hbus->state != hv_pcibus_removing)
		dev_err(&hbus->hdev->device,
			"%s() failed: %#llx", __func__, res);

	pci_msi_unmask_irq(data);
}

struct compose_comp_ctxt {
	struct hv_pci_compl comp_pkt;
	struct tran_int_desc int_desc;
};

static void hv_pci_compose_compl(void *context, struct pci_response *resp,
				 int resp_packet_size)
{
	struct compose_comp_ctxt *comp_pkt = context;
	struct pci_create_int_response *int_resp =
		(struct pci_create_int_response *)resp;

	comp_pkt->comp_pkt.completion_status = resp->status;
	comp_pkt->int_desc = int_resp->int_desc;
	complete(&comp_pkt->comp_pkt.host_event);
}

static u32 hv_compose_msi_req_v1(
	struct pci_create_interrupt *int_pkt, struct cpumask *affinity,
	u32 slot, u8 vector)
{
	int_pkt->message_type.type = PCI_CREATE_INTERRUPT_MESSAGE;
	int_pkt->wslot.slot = slot;
	int_pkt->int_desc.vector = vector;
	int_pkt->int_desc.vector_count = 1;
	int_pkt->int_desc.delivery_mode = dest_Fixed;

	/*
	 * Create MSI w/ dummy vCPU set, overwritten by subsequent retarget in
	 * hv_irq_unmask().
	 */
	int_pkt->int_desc.cpu_mask = CPU_AFFINITY_ALL;

	return sizeof(*int_pkt);
}

static u32 hv_compose_msi_req_v2(
	struct pci_create_interrupt2 *int_pkt, struct cpumask *affinity,
	u32 slot, u8 vector)
{
	int cpu;

	int_pkt->message_type.type = PCI_CREATE_INTERRUPT_MESSAGE2;
	int_pkt->wslot.slot = slot;
	int_pkt->int_desc.vector = vector;
	int_pkt->int_desc.vector_count = 1;
	int_pkt->int_desc.delivery_mode = dest_Fixed;

	/*
	 * Create MSI w/ dummy vCPU set targeting just one vCPU, overwritten
	 * by subsequent retarget in hv_irq_unmask().
	 */
	cpu = cpumask_first_and(affinity, cpu_online_mask);
	int_pkt->int_desc.processor_array[0] =
		hv_cpu_number_to_vp_number(cpu);
	int_pkt->int_desc.processor_count = 1;

	return sizeof(*int_pkt);
}

/**
 * hv_compose_msi_msg() - Supplies a valid MSI address/data
 * @data:	Everything about this MSI
 * @msg:	Buffer that is filled in by this function
 *
 * This function unpacks the IRQ looking for target CPU set, IDT
 * vector and mode and sends a message to the parent partition
 * asking for a mapping for that tuple in this partition.  The
 * response supplies a data value and address to which that data
 * should be written to trigger that interrupt.
 */
static void hv_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct irq_cfg *cfg = irqd_cfg(data);
	struct hv_pcibus_device *hbus;
	struct vmbus_channel *channel;
	struct hv_pci_dev *hpdev;
	struct pci_bus *pbus;
	struct pci_dev *pdev;
	struct cpumask *dest;
	struct compose_comp_ctxt comp;
	struct tran_int_desc *int_desc;
	struct {
		struct pci_packet pci_pkt;
		union {
			struct pci_create_interrupt v1;
			struct pci_create_interrupt2 v2;
		} int_pkts;
	} __packed ctxt;

	u32 size;
	int ret;

	pdev = msi_desc_to_pci_dev(irq_data_get_msi_desc(data));
	dest = irq_data_get_effective_affinity_mask(data);
	pbus = pdev->bus;
	hbus = container_of(pbus->sysdata, struct hv_pcibus_device, sysdata);
	channel = hbus->hdev->channel;
	hpdev = get_pcichild_wslot(hbus, devfn_to_wslot(pdev->devfn));
	if (!hpdev)
		goto return_null_message;

	/* Free any previous message that might have already been composed. */
	if (data->chip_data) {
		int_desc = data->chip_data;
		data->chip_data = NULL;
		hv_int_desc_free(hpdev, int_desc);
	}

	int_desc = kzalloc(sizeof(*int_desc), GFP_ATOMIC);
	if (!int_desc)
		goto drop_reference;

	memset(&ctxt, 0, sizeof(ctxt));
	init_completion(&comp.comp_pkt.host_event);
	ctxt.pci_pkt.completion_func = hv_pci_compose_compl;
	ctxt.pci_pkt.compl_ctxt = &comp;

	switch (hbus->protocol_version) {
	case PCI_PROTOCOL_VERSION_1_1:
		size = hv_compose_msi_req_v1(&ctxt.int_pkts.v1,
					dest,
					hpdev->desc.win_slot.slot,
					cfg->vector);
		break;

	case PCI_PROTOCOL_VERSION_1_2:
	case PCI_PROTOCOL_VERSION_1_3:
		size = hv_compose_msi_req_v2(&ctxt.int_pkts.v2,
					dest,
					hpdev->desc.win_slot.slot,
					cfg->vector);
		break;

	default:
		/* As we only negotiate protocol versions known to this driver,
		 * this path should never hit. However, this is it not a hot
		 * path so we print a message to aid future updates.
		 */
		dev_err(&hbus->hdev->device,
			"Unexpected vPCI protocol, update driver.");
		goto free_int_desc;
	}

	ret = vmbus_sendpacket(hpdev->hbus->hdev->channel, &ctxt.int_pkts,
			       size, (unsigned long)&ctxt.pci_pkt,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret) {
		dev_err(&hbus->hdev->device,
			"Sending request for interrupt failed: 0x%x",
			comp.comp_pkt.completion_status);
		goto free_int_desc;
	}

	/*
	 * Prevents hv_pci_onchannelcallback() from running concurrently
	 * in the tasklet.
	 */
	tasklet_disable(&channel->callback_event);

	/*
	 * Since this function is called with IRQ locks held, can't
	 * do normal wait for completion; instead poll.
	 */
	while (!try_wait_for_completion(&comp.comp_pkt.host_event)) {
		unsigned long flags;

		/* 0xFFFF means an invalid PCI VENDOR ID. */
		if (hv_pcifront_get_vendor_id(hpdev) == 0xFFFF) {
			dev_err_once(&hbus->hdev->device,
				     "the device has gone\n");
			goto enable_tasklet;
		}

		/*
		 * Make sure that the ring buffer data structure doesn't get
		 * freed while we dereference the ring buffer pointer.  Test
		 * for the channel's onchannel_callback being NULL within a
		 * sched_lock critical section.  See also the inline comments
		 * in vmbus_reset_channel_cb().
		 */
		spin_lock_irqsave(&channel->sched_lock, flags);
		if (unlikely(channel->onchannel_callback == NULL)) {
			spin_unlock_irqrestore(&channel->sched_lock, flags);
			goto enable_tasklet;
		}
		hv_pci_onchannelcallback(hbus);
		spin_unlock_irqrestore(&channel->sched_lock, flags);

		if (hpdev->state == hv_pcichild_ejecting) {
			dev_err_once(&hbus->hdev->device,
				     "the device is being ejected\n");
			goto enable_tasklet;
		}

		udelay(100);
	}

	tasklet_enable(&channel->callback_event);

	if (comp.comp_pkt.completion_status < 0) {
		dev_err(&hbus->hdev->device,
			"Request for interrupt failed: 0x%x",
			comp.comp_pkt.completion_status);
		goto free_int_desc;
	}

	/*
	 * Record the assignment so that this can be unwound later. Using
	 * irq_set_chip_data() here would be appropriate, but the lock it takes
	 * is already held.
	 */
	*int_desc = comp.int_desc;
	data->chip_data = int_desc;

	/* Pass up the result. */
	msg->address_hi = comp.int_desc.address >> 32;
	msg->address_lo = comp.int_desc.address & 0xffffffff;
	msg->data = comp.int_desc.data;

	put_pcichild(hpdev);
	return;

enable_tasklet:
	tasklet_enable(&channel->callback_event);
free_int_desc:
	kfree(int_desc);
drop_reference:
	put_pcichild(hpdev);
return_null_message:
	msg->address_hi = 0;
	msg->address_lo = 0;
	msg->data = 0;
}

/* HW Interrupt Chip Descriptor */
static struct irq_chip hv_msi_irq_chip = {
	.name			= "Hyper-V PCIe MSI",
	.irq_compose_msi_msg	= hv_compose_msi_msg,
	.irq_set_affinity	= hv_set_affinity,
	.irq_ack		= irq_chip_ack_parent,
	.irq_mask		= hv_irq_mask,
	.irq_unmask		= hv_irq_unmask,
};

static struct msi_domain_ops hv_msi_ops = {
	.msi_prepare	= hv_msi_prepare,
	.msi_free	= hv_msi_free,
};

/**
 * hv_pcie_init_irq_domain() - Initialize IRQ domain
 * @hbus:	The root PCI bus
 *
 * This function creates an IRQ domain which will be used for
 * interrupts from devices that have been passed through.  These
 * devices only support MSI and MSI-X, not line-based interrupts
 * or simulations of line-based interrupts through PCIe's
 * fabric-layer messages.  Because interrupts are remapped, we
 * can support multi-message MSI here.
 *
 * Return: '0' on success and error value on failure
 */
static int hv_pcie_init_irq_domain(struct hv_pcibus_device *hbus)
{
	hbus->msi_info.chip = &hv_msi_irq_chip;
	hbus->msi_info.ops = &hv_msi_ops;
	hbus->msi_info.flags = (MSI_FLAG_USE_DEF_DOM_OPS |
		MSI_FLAG_USE_DEF_CHIP_OPS | MSI_FLAG_MULTI_PCI_MSI |
		MSI_FLAG_PCI_MSIX);
	hbus->msi_info.handler = handle_edge_irq;
	hbus->msi_info.handler_name = "edge";
	hbus->msi_info.data = hbus;
	hbus->irq_domain = pci_msi_create_irq_domain(hbus->sysdata.fwnode,
						     &hbus->msi_info,
						     x86_vector_domain);
	if (!hbus->irq_domain) {
		dev_err(&hbus->hdev->device,
			"Failed to build an MSI IRQ domain\n");
		return -ENODEV;
	}

	return 0;
}

/**
 * get_bar_size() - Get the address space consumed by a BAR
 * @bar_val:	Value that a BAR returned after -1 was written
 *              to it.
 *
 * This function returns the size of the BAR, rounded up to 1
 * page.  It has to be rounded up because the hypervisor's page
 * table entry that maps the BAR into the VM can't specify an
 * offset within a page.  The invariant is that the hypervisor
 * must place any BARs of smaller than page length at the
 * beginning of a page.
 *
 * Return:	Size in bytes of the consumed MMIO space.
 */
static u64 get_bar_size(u64 bar_val)
{
	return round_up((1 + ~(bar_val & PCI_BASE_ADDRESS_MEM_MASK)),
			PAGE_SIZE);
}

/**
 * survey_child_resources() - Total all MMIO requirements
 * @hbus:	Root PCI bus, as understood by this driver
 */
static void survey_child_resources(struct hv_pcibus_device *hbus)
{
	struct hv_pci_dev *hpdev;
	resource_size_t bar_size = 0;
	unsigned long flags;
	struct completion *event;
	u64 bar_val;
	int i;

	/* If nobody is waiting on the answer, don't compute it. */
	event = xchg(&hbus->survey_event, NULL);
	if (!event)
		return;

	/* If the answer has already been computed, go with it. */
	if (hbus->low_mmio_space || hbus->high_mmio_space) {
		complete(event);
		return;
	}

	spin_lock_irqsave(&hbus->device_list_lock, flags);

	/*
	 * Due to an interesting quirk of the PCI spec, all memory regions
	 * for a child device are a power of 2 in size and aligned in memory,
	 * so it's sufficient to just add them up without tracking alignment.
	 */
	list_for_each_entry(hpdev, &hbus->children, list_entry) {
		for (i = 0; i < PCI_STD_NUM_BARS; i++) {
			if (hpdev->probed_bar[i] & PCI_BASE_ADDRESS_SPACE_IO)
				dev_err(&hbus->hdev->device,
					"There's an I/O BAR in this list!\n");

			if (hpdev->probed_bar[i] != 0) {
				/*
				 * A probed BAR has all the upper bits set that
				 * can be changed.
				 */

				bar_val = hpdev->probed_bar[i];
				if (bar_val & PCI_BASE_ADDRESS_MEM_TYPE_64)
					bar_val |=
					((u64)hpdev->probed_bar[++i] << 32);
				else
					bar_val |= 0xffffffff00000000ULL;

				bar_size = get_bar_size(bar_val);

				if (bar_val & PCI_BASE_ADDRESS_MEM_TYPE_64)
					hbus->high_mmio_space += bar_size;
				else
					hbus->low_mmio_space += bar_size;
			}
		}
	}

	spin_unlock_irqrestore(&hbus->device_list_lock, flags);
	complete(event);
}

/**
 * prepopulate_bars() - Fill in BARs with defaults
 * @hbus:	Root PCI bus, as understood by this driver
 *
 * The core PCI driver code seems much, much happier if the BARs
 * for a device have values upon first scan. So fill them in.
 * The algorithm below works down from large sizes to small,
 * attempting to pack the assignments optimally. The assumption,
 * enforced in other parts of the code, is that the beginning of
 * the memory-mapped I/O space will be aligned on the largest
 * BAR size.
 */
static void prepopulate_bars(struct hv_pcibus_device *hbus)
{
	resource_size_t high_size = 0;
	resource_size_t low_size = 0;
	resource_size_t high_base = 0;
	resource_size_t low_base = 0;
	resource_size_t bar_size;
	struct hv_pci_dev *hpdev;
	unsigned long flags;
	u64 bar_val;
	u32 command;
	bool high;
	int i;

	if (hbus->low_mmio_space) {
		low_size = 1ULL << (63 - __builtin_clzll(hbus->low_mmio_space));
		low_base = hbus->low_mmio_res->start;
	}

	if (hbus->high_mmio_space) {
		high_size = 1ULL <<
			(63 - __builtin_clzll(hbus->high_mmio_space));
		high_base = hbus->high_mmio_res->start;
	}

	spin_lock_irqsave(&hbus->device_list_lock, flags);

	/*
	 * Clear the memory enable bit, in case it's already set. This occurs
	 * in the suspend path of hibernation, where the device is suspended,
	 * resumed and suspended again: see hibernation_snapshot() and
	 * hibernation_platform_enter().
	 *
	 * If the memory enable bit is already set, Hyper-V sliently ignores
	 * the below BAR updates, and the related PCI device driver can not
	 * work, because reading from the device register(s) always returns
	 * 0xFFFFFFFF.
	 */
	list_for_each_entry(hpdev, &hbus->children, list_entry) {
		_hv_pcifront_read_config(hpdev, PCI_COMMAND, 2, &command);
		command &= ~PCI_COMMAND_MEMORY;
		_hv_pcifront_write_config(hpdev, PCI_COMMAND, 2, command);
	}

	/* Pick addresses for the BARs. */
	do {
		list_for_each_entry(hpdev, &hbus->children, list_entry) {
			for (i = 0; i < PCI_STD_NUM_BARS; i++) {
				bar_val = hpdev->probed_bar[i];
				if (bar_val == 0)
					continue;
				high = bar_val & PCI_BASE_ADDRESS_MEM_TYPE_64;
				if (high) {
					bar_val |=
						((u64)hpdev->probed_bar[i + 1]
						 << 32);
				} else {
					bar_val |= 0xffffffffULL << 32;
				}
				bar_size = get_bar_size(bar_val);
				if (high) {
					if (high_size != bar_size) {
						i++;
						continue;
					}
					_hv_pcifront_write_config(hpdev,
						PCI_BASE_ADDRESS_0 + (4 * i),
						4,
						(u32)(high_base & 0xffffff00));
					i++;
					_hv_pcifront_write_config(hpdev,
						PCI_BASE_ADDRESS_0 + (4 * i),
						4, (u32)(high_base >> 32));
					high_base += bar_size;
				} else {
					if (low_size != bar_size)
						continue;
					_hv_pcifront_write_config(hpdev,
						PCI_BASE_ADDRESS_0 + (4 * i),
						4,
						(u32)(low_base & 0xffffff00));
					low_base += bar_size;
				}
			}
			if (high_size <= 1 && low_size <= 1) {
				/* Set the memory enable bit. */
				_hv_pcifront_read_config(hpdev, PCI_COMMAND, 2,
							 &command);
				command |= PCI_COMMAND_MEMORY;
				_hv_pcifront_write_config(hpdev, PCI_COMMAND, 2,
							  command);
				break;
			}
		}

		high_size >>= 1;
		low_size >>= 1;
	}  while (high_size || low_size);

	spin_unlock_irqrestore(&hbus->device_list_lock, flags);
}

/*
 * Assign entries in sysfs pci slot directory.
 *
 * Note that this function does not need to lock the children list
 * because it is called from pci_devices_present_work which
 * is serialized with hv_eject_device_work because they are on the
 * same ordered workqueue. Therefore hbus->children list will not change
 * even when pci_create_slot sleeps.
 */
static void hv_pci_assign_slots(struct hv_pcibus_device *hbus)
{
	struct hv_pci_dev *hpdev;
	char name[SLOT_NAME_SIZE];
	int slot_nr;

	list_for_each_entry(hpdev, &hbus->children, list_entry) {
		if (hpdev->pci_slot)
			continue;

		slot_nr = PCI_SLOT(wslot_to_devfn(hpdev->desc.win_slot.slot));
		snprintf(name, SLOT_NAME_SIZE, "%u", hpdev->desc.ser);
		hpdev->pci_slot = pci_create_slot(hbus->pci_bus, slot_nr,
					  name, NULL);
		if (IS_ERR(hpdev->pci_slot)) {
			pr_warn("pci_create slot %s failed\n", name);
			hpdev->pci_slot = NULL;
		}
	}
}

/*
 * Remove entries in sysfs pci slot directory.
 */
static void hv_pci_remove_slots(struct hv_pcibus_device *hbus)
{
	struct hv_pci_dev *hpdev;

	list_for_each_entry(hpdev, &hbus->children, list_entry) {
		if (!hpdev->pci_slot)
			continue;
		pci_destroy_slot(hpdev->pci_slot);
		hpdev->pci_slot = NULL;
	}
}

/*
 * Set NUMA node for the devices on the bus
 */
static void hv_pci_assign_numa_node(struct hv_pcibus_device *hbus)
{
	struct pci_dev *dev;
	struct pci_bus *bus = hbus->pci_bus;
	struct hv_pci_dev *hv_dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		hv_dev = get_pcichild_wslot(hbus, devfn_to_wslot(dev->devfn));
		if (!hv_dev)
			continue;

		if (hv_dev->desc.flags & HV_PCI_DEVICE_FLAG_NUMA_AFFINITY &&
		    hv_dev->desc.virtual_numa_node < num_possible_nodes())
			/*
			 * The kernel may boot with some NUMA nodes offline
			 * (e.g. in a KDUMP kernel) or with NUMA disabled via
			 * "numa=off". In those cases, adjust the host provided
			 * NUMA node to a valid NUMA node used by the kernel.
			 */
			set_dev_node(&dev->dev,
				     numa_map_to_online_node(
					     hv_dev->desc.virtual_numa_node));

		put_pcichild(hv_dev);
	}
}

/**
 * create_root_hv_pci_bus() - Expose a new root PCI bus
 * @hbus:	Root PCI bus, as understood by this driver
 *
 * Return: 0 on success, -errno on failure
 */
static int create_root_hv_pci_bus(struct hv_pcibus_device *hbus)
{
	/* Register the device */
	hbus->pci_bus = pci_create_root_bus(&hbus->hdev->device,
					    0, /* bus number is always zero */
					    &hv_pcifront_ops,
					    &hbus->sysdata,
					    &hbus->resources_for_children);
	if (!hbus->pci_bus)
		return -ENODEV;

	hbus->pci_bus->msi = &hbus->msi_chip;
	hbus->pci_bus->msi->dev = &hbus->hdev->device;

	pci_lock_rescan_remove();
	pci_scan_child_bus(hbus->pci_bus);
	hv_pci_assign_numa_node(hbus);
	pci_bus_assign_resources(hbus->pci_bus);
	hv_pci_assign_slots(hbus);
	pci_bus_add_devices(hbus->pci_bus);
	pci_unlock_rescan_remove();
	hbus->state = hv_pcibus_installed;
	return 0;
}

struct q_res_req_compl {
	struct completion host_event;
	struct hv_pci_dev *hpdev;
};

/**
 * q_resource_requirements() - Query Resource Requirements
 * @context:		The completion context.
 * @resp:		The response that came from the host.
 * @resp_packet_size:	The size in bytes of resp.
 *
 * This function is invoked on completion of a Query Resource
 * Requirements packet.
 */
static void q_resource_requirements(void *context, struct pci_response *resp,
				    int resp_packet_size)
{
	struct q_res_req_compl *completion = context;
	struct pci_q_res_req_response *q_res_req =
		(struct pci_q_res_req_response *)resp;
	int i;

	if (resp->status < 0) {
		dev_err(&completion->hpdev->hbus->hdev->device,
			"query resource requirements failed: %x\n",
			resp->status);
	} else {
		for (i = 0; i < PCI_STD_NUM_BARS; i++) {
			completion->hpdev->probed_bar[i] =
				q_res_req->probed_bar[i];
		}
	}

	complete(&completion->host_event);
}

/**
 * new_pcichild_device() - Create a new child device
 * @hbus:	The internal struct tracking this root PCI bus.
 * @desc:	The information supplied so far from the host
 *              about the device.
 *
 * This function creates the tracking structure for a new child
 * device and kicks off the process of figuring out what it is.
 *
 * Return: Pointer to the new tracking struct
 */
static struct hv_pci_dev *new_pcichild_device(struct hv_pcibus_device *hbus,
		struct hv_pcidev_description *desc)
{
	struct hv_pci_dev *hpdev;
	struct pci_child_message *res_req;
	struct q_res_req_compl comp_pkt;
	struct {
		struct pci_packet init_packet;
		u8 buffer[sizeof(struct pci_child_message)];
	} pkt;
	unsigned long flags;
	int ret;

	hpdev = kzalloc(sizeof(*hpdev), GFP_KERNEL);
	if (!hpdev)
		return NULL;

	hpdev->hbus = hbus;

	memset(&pkt, 0, sizeof(pkt));
	init_completion(&comp_pkt.host_event);
	comp_pkt.hpdev = hpdev;
	pkt.init_packet.compl_ctxt = &comp_pkt;
	pkt.init_packet.completion_func = q_resource_requirements;
	res_req = (struct pci_child_message *)&pkt.init_packet.message;
	res_req->message_type.type = PCI_QUERY_RESOURCE_REQUIREMENTS;
	res_req->wslot.slot = desc->win_slot.slot;

	ret = vmbus_sendpacket(hbus->hdev->channel, res_req,
			       sizeof(struct pci_child_message),
			       (unsigned long)&pkt.init_packet,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret)
		goto error;

	if (wait_for_response(hbus->hdev, &comp_pkt.host_event))
		goto error;

	hpdev->desc = *desc;
	refcount_set(&hpdev->refs, 1);
	get_pcichild(hpdev);
	spin_lock_irqsave(&hbus->device_list_lock, flags);

	list_add_tail(&hpdev->list_entry, &hbus->children);
	spin_unlock_irqrestore(&hbus->device_list_lock, flags);
	return hpdev;

error:
	kfree(hpdev);
	return NULL;
}

/**
 * get_pcichild_wslot() - Find device from slot
 * @hbus:	Root PCI bus, as understood by this driver
 * @wslot:	Location on the bus
 *
 * This function looks up a PCI device and returns the internal
 * representation of it.  It acquires a reference on it, so that
 * the device won't be deleted while somebody is using it.  The
 * caller is responsible for calling put_pcichild() to release
 * this reference.
 *
 * Return:	Internal representation of a PCI device
 */
static struct hv_pci_dev *get_pcichild_wslot(struct hv_pcibus_device *hbus,
					     u32 wslot)
{
	unsigned long flags;
	struct hv_pci_dev *iter, *hpdev = NULL;

	spin_lock_irqsave(&hbus->device_list_lock, flags);
	list_for_each_entry(iter, &hbus->children, list_entry) {
		if (iter->desc.win_slot.slot == wslot) {
			hpdev = iter;
			get_pcichild(hpdev);
			break;
		}
	}
	spin_unlock_irqrestore(&hbus->device_list_lock, flags);

	return hpdev;
}

/**
 * pci_devices_present_work() - Handle new list of child devices
 * @work:	Work struct embedded in struct hv_dr_work
 *
 * "Bus Relations" is the Windows term for "children of this
 * bus."  The terminology is preserved here for people trying to
 * debug the interaction between Hyper-V and Linux.  This
 * function is called when the parent partition reports a list
 * of functions that should be observed under this PCI Express
 * port (bus).
 *
 * This function updates the list, and must tolerate being
 * called multiple times with the same information.  The typical
 * number of child devices is one, with very atypical cases
 * involving three or four, so the algorithms used here can be
 * simple and inefficient.
 *
 * It must also treat the omission of a previously observed device as
 * notification that the device no longer exists.
 *
 * Note that this function is serialized with hv_eject_device_work(),
 * because both are pushed to the ordered workqueue hbus->wq.
 */
static void pci_devices_present_work(struct work_struct *work)
{
	u32 child_no;
	bool found;
	struct hv_pcidev_description *new_desc;
	struct hv_pci_dev *hpdev;
	struct hv_pcibus_device *hbus;
	struct list_head removed;
	struct hv_dr_work *dr_wrk;
	struct hv_dr_state *dr = NULL;
	unsigned long flags;

	dr_wrk = container_of(work, struct hv_dr_work, wrk);
	hbus = dr_wrk->bus;
	kfree(dr_wrk);

	INIT_LIST_HEAD(&removed);

	/* Pull this off the queue and process it if it was the last one. */
	spin_lock_irqsave(&hbus->device_list_lock, flags);
	while (!list_empty(&hbus->dr_list)) {
		dr = list_first_entry(&hbus->dr_list, struct hv_dr_state,
				      list_entry);
		list_del(&dr->list_entry);

		/* Throw this away if the list still has stuff in it. */
		if (!list_empty(&hbus->dr_list)) {
			kfree(dr);
			continue;
		}
	}
	spin_unlock_irqrestore(&hbus->device_list_lock, flags);

	if (!dr) {
		put_hvpcibus(hbus);
		return;
	}

	/* First, mark all existing children as reported missing. */
	spin_lock_irqsave(&hbus->device_list_lock, flags);
	list_for_each_entry(hpdev, &hbus->children, list_entry) {
		hpdev->reported_missing = true;
	}
	spin_unlock_irqrestore(&hbus->device_list_lock, flags);

	/* Next, add back any reported devices. */
	for (child_no = 0; child_no < dr->device_count; child_no++) {
		found = false;
		new_desc = &dr->func[child_no];

		spin_lock_irqsave(&hbus->device_list_lock, flags);
		list_for_each_entry(hpdev, &hbus->children, list_entry) {
			if ((hpdev->desc.win_slot.slot == new_desc->win_slot.slot) &&
			    (hpdev->desc.v_id == new_desc->v_id) &&
			    (hpdev->desc.d_id == new_desc->d_id) &&
			    (hpdev->desc.ser == new_desc->ser)) {
				hpdev->reported_missing = false;
				found = true;
			}
		}
		spin_unlock_irqrestore(&hbus->device_list_lock, flags);

		if (!found) {
			hpdev = new_pcichild_device(hbus, new_desc);
			if (!hpdev)
				dev_err(&hbus->hdev->device,
					"couldn't record a child device.\n");
		}
	}

	/* Move missing children to a list on the stack. */
	spin_lock_irqsave(&hbus->device_list_lock, flags);
	do {
		found = false;
		list_for_each_entry(hpdev, &hbus->children, list_entry) {
			if (hpdev->reported_missing) {
				found = true;
				put_pcichild(hpdev);
				list_move_tail(&hpdev->list_entry, &removed);
				break;
			}
		}
	} while (found);
	spin_unlock_irqrestore(&hbus->device_list_lock, flags);

	/* Delete everything that should no longer exist. */
	while (!list_empty(&removed)) {
		hpdev = list_first_entry(&removed, struct hv_pci_dev,
					 list_entry);
		list_del(&hpdev->list_entry);

		if (hpdev->pci_slot)
			pci_destroy_slot(hpdev->pci_slot);

		put_pcichild(hpdev);
	}

	switch (hbus->state) {
	case hv_pcibus_installed:
		/*
		 * Tell the core to rescan bus
		 * because there may have been changes.
		 */
		pci_lock_rescan_remove();
		pci_scan_child_bus(hbus->pci_bus);
		hv_pci_assign_numa_node(hbus);
		hv_pci_assign_slots(hbus);
		pci_unlock_rescan_remove();
		break;

	case hv_pcibus_init:
	case hv_pcibus_probed:
		survey_child_resources(hbus);
		break;

	default:
		break;
	}

	put_hvpcibus(hbus);
	kfree(dr);
}

/**
 * hv_pci_start_relations_work() - Queue work to start device discovery
 * @hbus:	Root PCI bus, as understood by this driver
 * @dr:		The list of children returned from host
 *
 * Return:  0 on success, -errno on failure
 */
static int hv_pci_start_relations_work(struct hv_pcibus_device *hbus,
				       struct hv_dr_state *dr)
{
	struct hv_dr_work *dr_wrk;
	unsigned long flags;
	bool pending_dr;

	if (hbus->state == hv_pcibus_removing) {
		dev_info(&hbus->hdev->device,
			 "PCI VMBus BUS_RELATIONS: ignored\n");
		return -ENOENT;
	}

	dr_wrk = kzalloc(sizeof(*dr_wrk), GFP_NOWAIT);
	if (!dr_wrk)
		return -ENOMEM;

	INIT_WORK(&dr_wrk->wrk, pci_devices_present_work);
	dr_wrk->bus = hbus;

	spin_lock_irqsave(&hbus->device_list_lock, flags);
	/*
	 * If pending_dr is true, we have already queued a work,
	 * which will see the new dr. Otherwise, we need to
	 * queue a new work.
	 */
	pending_dr = !list_empty(&hbus->dr_list);
	list_add_tail(&dr->list_entry, &hbus->dr_list);
	spin_unlock_irqrestore(&hbus->device_list_lock, flags);

	if (pending_dr) {
		kfree(dr_wrk);
	} else {
		get_hvpcibus(hbus);
		queue_work(hbus->wq, &dr_wrk->wrk);
	}

	return 0;
}

/**
 * hv_pci_devices_present() - Handle list of new children
 * @hbus:      Root PCI bus, as understood by this driver
 * @relations: Packet from host listing children
 *
 * Process a new list of devices on the bus. The list of devices is
 * discovered by VSP and sent to us via VSP message PCI_BUS_RELATIONS,
 * whenever a new list of devices for this bus appears.
 */
static void hv_pci_devices_present(struct hv_pcibus_device *hbus,
				   struct pci_bus_relations *relations)
{
	struct hv_dr_state *dr;
	int i;

	dr = kzalloc(struct_size(dr, func, relations->device_count),
		     GFP_NOWAIT);
	if (!dr)
		return;

	dr->device_count = relations->device_count;
	for (i = 0; i < dr->device_count; i++) {
		dr->func[i].v_id = relations->func[i].v_id;
		dr->func[i].d_id = relations->func[i].d_id;
		dr->func[i].rev = relations->func[i].rev;
		dr->func[i].prog_intf = relations->func[i].prog_intf;
		dr->func[i].subclass = relations->func[i].subclass;
		dr->func[i].base_class = relations->func[i].base_class;
		dr->func[i].subsystem_id = relations->func[i].subsystem_id;
		dr->func[i].win_slot = relations->func[i].win_slot;
		dr->func[i].ser = relations->func[i].ser;
	}

	if (hv_pci_start_relations_work(hbus, dr))
		kfree(dr);
}

/**
 * hv_pci_devices_present2() - Handle list of new children
 * @hbus:	Root PCI bus, as understood by this driver
 * @relations:	Packet from host listing children
 *
 * This function is the v2 version of hv_pci_devices_present()
 */
static void hv_pci_devices_present2(struct hv_pcibus_device *hbus,
				    struct pci_bus_relations2 *relations)
{
	struct hv_dr_state *dr;
	int i;

	dr = kzalloc(struct_size(dr, func, relations->device_count),
		     GFP_NOWAIT);
	if (!dr)
		return;

	dr->device_count = relations->device_count;
	for (i = 0; i < dr->device_count; i++) {
		dr->func[i].v_id = relations->func[i].v_id;
		dr->func[i].d_id = relations->func[i].d_id;
		dr->func[i].rev = relations->func[i].rev;
		dr->func[i].prog_intf = relations->func[i].prog_intf;
		dr->func[i].subclass = relations->func[i].subclass;
		dr->func[i].base_class = relations->func[i].base_class;
		dr->func[i].subsystem_id = relations->func[i].subsystem_id;
		dr->func[i].win_slot = relations->func[i].win_slot;
		dr->func[i].ser = relations->func[i].ser;
		dr->func[i].flags = relations->func[i].flags;
		dr->func[i].virtual_numa_node =
			relations->func[i].virtual_numa_node;
	}

	if (hv_pci_start_relations_work(hbus, dr))
		kfree(dr);
}

/**
 * hv_eject_device_work() - Asynchronously handles ejection
 * @work:	Work struct embedded in internal device struct
 *
 * This function handles ejecting a device.  Windows will
 * attempt to gracefully eject a device, waiting 60 seconds to
 * hear back from the guest OS that this completed successfully.
 * If this timer expires, the device will be forcibly removed.
 */
static void hv_eject_device_work(struct work_struct *work)
{
	struct pci_eject_response *ejct_pkt;
	struct hv_pcibus_device *hbus;
	struct hv_pci_dev *hpdev;
	struct pci_dev *pdev;
	unsigned long flags;
	int wslot;
	struct {
		struct pci_packet pkt;
		u8 buffer[sizeof(struct pci_eject_response)];
	} ctxt;

	hpdev = container_of(work, struct hv_pci_dev, wrk);
	hbus = hpdev->hbus;

	WARN_ON(hpdev->state != hv_pcichild_ejecting);

	/*
	 * Ejection can come before or after the PCI bus has been set up, so
	 * attempt to find it and tear down the bus state, if it exists.  This
	 * must be done without constructs like pci_domain_nr(hbus->pci_bus)
	 * because hbus->pci_bus may not exist yet.
	 */
	wslot = wslot_to_devfn(hpdev->desc.win_slot.slot);
	pdev = pci_get_domain_bus_and_slot(hbus->sysdata.domain, 0, wslot);
	if (pdev) {
		pci_lock_rescan_remove();
		pci_stop_and_remove_bus_device(pdev);
		pci_dev_put(pdev);
		pci_unlock_rescan_remove();
	}

	spin_lock_irqsave(&hbus->device_list_lock, flags);
	list_del(&hpdev->list_entry);
	spin_unlock_irqrestore(&hbus->device_list_lock, flags);

	if (hpdev->pci_slot)
		pci_destroy_slot(hpdev->pci_slot);

	memset(&ctxt, 0, sizeof(ctxt));
	ejct_pkt = (struct pci_eject_response *)&ctxt.pkt.message;
	ejct_pkt->message_type.type = PCI_EJECTION_COMPLETE;
	ejct_pkt->wslot.slot = hpdev->desc.win_slot.slot;
	vmbus_sendpacket(hbus->hdev->channel, ejct_pkt,
			 sizeof(*ejct_pkt), (unsigned long)&ctxt.pkt,
			 VM_PKT_DATA_INBAND, 0);

	/* For the get_pcichild() in hv_pci_eject_device() */
	put_pcichild(hpdev);
	/* For the two refs got in new_pcichild_device() */
	put_pcichild(hpdev);
	put_pcichild(hpdev);
	/* hpdev has been freed. Do not use it any more. */

	put_hvpcibus(hbus);
}

/**
 * hv_pci_eject_device() - Handles device ejection
 * @hpdev:	Internal device tracking struct
 *
 * This function is invoked when an ejection packet arrives.  It
 * just schedules work so that we don't re-enter the packet
 * delivery code handling the ejection.
 */
static void hv_pci_eject_device(struct hv_pci_dev *hpdev)
{
	struct hv_pcibus_device *hbus = hpdev->hbus;
	struct hv_device *hdev = hbus->hdev;

	if (hbus->state == hv_pcibus_removing) {
		dev_info(&hdev->device, "PCI VMBus EJECT: ignored\n");
		return;
	}

	hpdev->state = hv_pcichild_ejecting;
	get_pcichild(hpdev);
	INIT_WORK(&hpdev->wrk, hv_eject_device_work);
	get_hvpcibus(hbus);
	queue_work(hbus->wq, &hpdev->wrk);
}

/**
 * hv_pci_onchannelcallback() - Handles incoming packets
 * @context:	Internal bus tracking struct
 *
 * This function is invoked whenever the host sends a packet to
 * this channel (which is private to this root PCI bus).
 */
static void hv_pci_onchannelcallback(void *context)
{
	const int packet_size = 0x100;
	int ret;
	struct hv_pcibus_device *hbus = context;
	u32 bytes_recvd;
	u64 req_id;
	struct vmpacket_descriptor *desc;
	unsigned char *buffer;
	int bufferlen = packet_size;
	struct pci_packet *comp_packet;
	struct pci_response *response;
	struct pci_incoming_message *new_message;
	struct pci_bus_relations *bus_rel;
	struct pci_bus_relations2 *bus_rel2;
	struct pci_dev_inval_block *inval;
	struct pci_dev_incoming *dev_message;
	struct hv_pci_dev *hpdev;

	buffer = kmalloc(bufferlen, GFP_ATOMIC);
	if (!buffer)
		return;

	while (1) {
		ret = vmbus_recvpacket_raw(hbus->hdev->channel, buffer,
					   bufferlen, &bytes_recvd, &req_id);

		if (ret == -ENOBUFS) {
			kfree(buffer);
			/* Handle large packet */
			bufferlen = bytes_recvd;
			buffer = kmalloc(bytes_recvd, GFP_ATOMIC);
			if (!buffer)
				return;
			continue;
		}

		/* Zero length indicates there are no more packets. */
		if (ret || !bytes_recvd)
			break;

		/*
		 * All incoming packets must be at least as large as a
		 * response.
		 */
		if (bytes_recvd <= sizeof(struct pci_response))
			continue;
		desc = (struct vmpacket_descriptor *)buffer;

		switch (desc->type) {
		case VM_PKT_COMP:

			/*
			 * The host is trusted, and thus it's safe to interpret
			 * this transaction ID as a pointer.
			 */
			comp_packet = (struct pci_packet *)req_id;
			response = (struct pci_response *)buffer;
			comp_packet->completion_func(comp_packet->compl_ctxt,
						     response,
						     bytes_recvd);
			break;

		case VM_PKT_DATA_INBAND:

			new_message = (struct pci_incoming_message *)buffer;
			switch (new_message->message_type.type) {
			case PCI_BUS_RELATIONS:

				bus_rel = (struct pci_bus_relations *)buffer;
				if (bytes_recvd <
					struct_size(bus_rel, func,
						    bus_rel->device_count)) {
					dev_err(&hbus->hdev->device,
						"bus relations too small\n");
					break;
				}

				hv_pci_devices_present(hbus, bus_rel);
				break;

			case PCI_BUS_RELATIONS2:

				bus_rel2 = (struct pci_bus_relations2 *)buffer;
				if (bytes_recvd <
					struct_size(bus_rel2, func,
						    bus_rel2->device_count)) {
					dev_err(&hbus->hdev->device,
						"bus relations v2 too small\n");
					break;
				}

				hv_pci_devices_present2(hbus, bus_rel2);
				break;

			case PCI_EJECT:

				dev_message = (struct pci_dev_incoming *)buffer;
				hpdev = get_pcichild_wslot(hbus,
						      dev_message->wslot.slot);
				if (hpdev) {
					hv_pci_eject_device(hpdev);
					put_pcichild(hpdev);
				}
				break;

			case PCI_INVALIDATE_BLOCK:

				inval = (struct pci_dev_inval_block *)buffer;
				hpdev = get_pcichild_wslot(hbus,
							   inval->wslot.slot);
				if (hpdev) {
					if (hpdev->block_invalidate) {
						hpdev->block_invalidate(
						    hpdev->invalidate_context,
						    inval->block_mask);
					}
					put_pcichild(hpdev);
				}
				break;

			default:
				dev_warn(&hbus->hdev->device,
					"Unimplemented protocol message %x\n",
					new_message->message_type.type);
				break;
			}
			break;

		default:
			dev_err(&hbus->hdev->device,
				"unhandled packet type %d, tid %llx len %d\n",
				desc->type, req_id, bytes_recvd);
			break;
		}
	}

	kfree(buffer);
}

/**
 * hv_pci_protocol_negotiation() - Set up protocol
 * @hdev:		VMBus's tracking struct for this root PCI bus.
 * @version:		Array of supported channel protocol versions in
 *			the order of probing - highest go first.
 * @num_version:	Number of elements in the version array.
 *
 * This driver is intended to support running on Windows 10
 * (server) and later versions. It will not run on earlier
 * versions, as they assume that many of the operations which
 * Linux needs accomplished with a spinlock held were done via
 * asynchronous messaging via VMBus.  Windows 10 increases the
 * surface area of PCI emulation so that these actions can take
 * place by suspending a virtual processor for their duration.
 *
 * This function negotiates the channel protocol version,
 * failing if the host doesn't support the necessary protocol
 * level.
 */
static int hv_pci_protocol_negotiation(struct hv_device *hdev,
				       enum pci_protocol_version_t version[],
				       int num_version)
{
	struct hv_pcibus_device *hbus = hv_get_drvdata(hdev);
	struct pci_version_request *version_req;
	struct hv_pci_compl comp_pkt;
	struct pci_packet *pkt;
	int ret;
	int i;

	/*
	 * Initiate the handshake with the host and negotiate
	 * a version that the host can support. We start with the
	 * highest version number and go down if the host cannot
	 * support it.
	 */
	pkt = kzalloc(sizeof(*pkt) + sizeof(*version_req), GFP_KERNEL);
	if (!pkt)
		return -ENOMEM;

	init_completion(&comp_pkt.host_event);
	pkt->completion_func = hv_pci_generic_compl;
	pkt->compl_ctxt = &comp_pkt;
	version_req = (struct pci_version_request *)&pkt->message;
	version_req->message_type.type = PCI_QUERY_PROTOCOL_VERSION;

	for (i = 0; i < num_version; i++) {
		version_req->protocol_version = version[i];
		ret = vmbus_sendpacket(hdev->channel, version_req,
				sizeof(struct pci_version_request),
				(unsigned long)pkt, VM_PKT_DATA_INBAND,
				VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
		if (!ret)
			ret = wait_for_response(hdev, &comp_pkt.host_event);

		if (ret) {
			dev_err(&hdev->device,
				"PCI Pass-through VSP failed to request version: %d",
				ret);
			goto exit;
		}

		if (comp_pkt.completion_status >= 0) {
			hbus->protocol_version = version[i];
			dev_info(&hdev->device,
				"PCI VMBus probing: Using version %#x\n",
				hbus->protocol_version);
			goto exit;
		}

		if (comp_pkt.completion_status != STATUS_REVISION_MISMATCH) {
			dev_err(&hdev->device,
				"PCI Pass-through VSP failed version request: %#x",
				comp_pkt.completion_status);
			ret = -EPROTO;
			goto exit;
		}

		reinit_completion(&comp_pkt.host_event);
	}

	dev_err(&hdev->device,
		"PCI pass-through VSP failed to find supported version");
	ret = -EPROTO;

exit:
	kfree(pkt);
	return ret;
}

/**
 * hv_pci_free_bridge_windows() - Release memory regions for the
 * bus
 * @hbus:	Root PCI bus, as understood by this driver
 */
static void hv_pci_free_bridge_windows(struct hv_pcibus_device *hbus)
{
	/*
	 * Set the resources back to the way they looked when they
	 * were allocated by setting IORESOURCE_BUSY again.
	 */

	if (hbus->low_mmio_space && hbus->low_mmio_res) {
		hbus->low_mmio_res->flags |= IORESOURCE_BUSY;
		vmbus_free_mmio(hbus->low_mmio_res->start,
				resource_size(hbus->low_mmio_res));
	}

	if (hbus->high_mmio_space && hbus->high_mmio_res) {
		hbus->high_mmio_res->flags |= IORESOURCE_BUSY;
		vmbus_free_mmio(hbus->high_mmio_res->start,
				resource_size(hbus->high_mmio_res));
	}
}

/**
 * hv_pci_allocate_bridge_windows() - Allocate memory regions
 * for the bus
 * @hbus:	Root PCI bus, as understood by this driver
 *
 * This function calls vmbus_allocate_mmio(), which is itself a
 * bit of a compromise.  Ideally, we might change the pnp layer
 * in the kernel such that it comprehends either PCI devices
 * which are "grandchildren of ACPI," with some intermediate bus
 * node (in this case, VMBus) or change it such that it
 * understands VMBus.  The pnp layer, however, has been declared
 * deprecated, and not subject to change.
 *
 * The workaround, implemented here, is to ask VMBus to allocate
 * MMIO space for this bus.  VMBus itself knows which ranges are
 * appropriate by looking at its own ACPI objects.  Then, after
 * these ranges are claimed, they're modified to look like they
 * would have looked if the ACPI and pnp code had allocated
 * bridge windows.  These descriptors have to exist in this form
 * in order to satisfy the code which will get invoked when the
 * endpoint PCI function driver calls request_mem_region() or
 * request_mem_region_exclusive().
 *
 * Return: 0 on success, -errno on failure
 */
static int hv_pci_allocate_bridge_windows(struct hv_pcibus_device *hbus)
{
	resource_size_t align;
	int ret;

	if (hbus->low_mmio_space) {
		align = 1ULL << (63 - __builtin_clzll(hbus->low_mmio_space));
		ret = vmbus_allocate_mmio(&hbus->low_mmio_res, hbus->hdev, 0,
					  (u64)(u32)0xffffffff,
					  hbus->low_mmio_space,
					  align, false);
		if (ret) {
			dev_err(&hbus->hdev->device,
				"Need %#llx of low MMIO space. Consider reconfiguring the VM.\n",
				hbus->low_mmio_space);
			return ret;
		}

		/* Modify this resource to become a bridge window. */
		hbus->low_mmio_res->flags |= IORESOURCE_WINDOW;
		hbus->low_mmio_res->flags &= ~IORESOURCE_BUSY;
		pci_add_resource(&hbus->resources_for_children,
				 hbus->low_mmio_res);
	}

	if (hbus->high_mmio_space) {
		align = 1ULL << (63 - __builtin_clzll(hbus->high_mmio_space));
		ret = vmbus_allocate_mmio(&hbus->high_mmio_res, hbus->hdev,
					  0x100000000, -1,
					  hbus->high_mmio_space, align,
					  false);
		if (ret) {
			dev_err(&hbus->hdev->device,
				"Need %#llx of high MMIO space. Consider reconfiguring the VM.\n",
				hbus->high_mmio_space);
			goto release_low_mmio;
		}

		/* Modify this resource to become a bridge window. */
		hbus->high_mmio_res->flags |= IORESOURCE_WINDOW;
		hbus->high_mmio_res->flags &= ~IORESOURCE_BUSY;
		pci_add_resource(&hbus->resources_for_children,
				 hbus->high_mmio_res);
	}

	return 0;

release_low_mmio:
	if (hbus->low_mmio_res) {
		vmbus_free_mmio(hbus->low_mmio_res->start,
				resource_size(hbus->low_mmio_res));
	}

	return ret;
}

/**
 * hv_allocate_config_window() - Find MMIO space for PCI Config
 * @hbus:	Root PCI bus, as understood by this driver
 *
 * This function claims memory-mapped I/O space for accessing
 * configuration space for the functions on this bus.
 *
 * Return: 0 on success, -errno on failure
 */
static int hv_allocate_config_window(struct hv_pcibus_device *hbus)
{
	int ret;

	/*
	 * Set up a region of MMIO space to use for accessing configuration
	 * space.
	 */
	ret = vmbus_allocate_mmio(&hbus->mem_config, hbus->hdev, 0, -1,
				  PCI_CONFIG_MMIO_LENGTH, 0x1000, false);
	if (ret)
		return ret;

	/*
	 * vmbus_allocate_mmio() gets used for allocating both device endpoint
	 * resource claims (those which cannot be overlapped) and the ranges
	 * which are valid for the children of this bus, which are intended
	 * to be overlapped by those children.  Set the flag on this claim
	 * meaning that this region can't be overlapped.
	 */

	hbus->mem_config->flags |= IORESOURCE_BUSY;

	return 0;
}

static void hv_free_config_window(struct hv_pcibus_device *hbus)
{
	vmbus_free_mmio(hbus->mem_config->start, PCI_CONFIG_MMIO_LENGTH);
}

static int hv_pci_bus_exit(struct hv_device *hdev, bool keep_devs);

/**
 * hv_pci_enter_d0() - Bring the "bus" into the D0 power state
 * @hdev:	VMBus's tracking struct for this root PCI bus
 *
 * Return: 0 on success, -errno on failure
 */
static int hv_pci_enter_d0(struct hv_device *hdev)
{
	struct hv_pcibus_device *hbus = hv_get_drvdata(hdev);
	struct pci_bus_d0_entry *d0_entry;
	struct hv_pci_compl comp_pkt;
	struct pci_packet *pkt;
	int ret;

	/*
	 * Tell the host that the bus is ready to use, and moved into the
	 * powered-on state.  This includes telling the host which region
	 * of memory-mapped I/O space has been chosen for configuration space
	 * access.
	 */
	pkt = kzalloc(sizeof(*pkt) + sizeof(*d0_entry), GFP_KERNEL);
	if (!pkt)
		return -ENOMEM;

	init_completion(&comp_pkt.host_event);
	pkt->completion_func = hv_pci_generic_compl;
	pkt->compl_ctxt = &comp_pkt;
	d0_entry = (struct pci_bus_d0_entry *)&pkt->message;
	d0_entry->message_type.type = PCI_BUS_D0ENTRY;
	d0_entry->mmio_base = hbus->mem_config->start;

	ret = vmbus_sendpacket(hdev->channel, d0_entry, sizeof(*d0_entry),
			       (unsigned long)pkt, VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (!ret)
		ret = wait_for_response(hdev, &comp_pkt.host_event);

	if (ret)
		goto exit;

	if (comp_pkt.completion_status < 0) {
		dev_err(&hdev->device,
			"PCI Pass-through VSP failed D0 Entry with status %x\n",
			comp_pkt.completion_status);
		ret = -EPROTO;
		goto exit;
	}

	ret = 0;

exit:
	kfree(pkt);
	return ret;
}

/**
 * hv_pci_query_relations() - Ask host to send list of child
 * devices
 * @hdev:	VMBus's tracking struct for this root PCI bus
 *
 * Return: 0 on success, -errno on failure
 */
static int hv_pci_query_relations(struct hv_device *hdev)
{
	struct hv_pcibus_device *hbus = hv_get_drvdata(hdev);
	struct pci_message message;
	struct completion comp;
	int ret;

	/* Ask the host to send along the list of child devices */
	init_completion(&comp);
	if (cmpxchg(&hbus->survey_event, NULL, &comp))
		return -ENOTEMPTY;

	memset(&message, 0, sizeof(message));
	message.type = PCI_QUERY_BUS_RELATIONS;

	ret = vmbus_sendpacket(hdev->channel, &message, sizeof(message),
			       0, VM_PKT_DATA_INBAND, 0);
	if (!ret)
		ret = wait_for_response(hdev, &comp);

	return ret;
}

/**
 * hv_send_resources_allocated() - Report local resource choices
 * @hdev:	VMBus's tracking struct for this root PCI bus
 *
 * The host OS is expecting to be sent a request as a message
 * which contains all the resources that the device will use.
 * The response contains those same resources, "translated"
 * which is to say, the values which should be used by the
 * hardware, when it delivers an interrupt.  (MMIO resources are
 * used in local terms.)  This is nice for Windows, and lines up
 * with the FDO/PDO split, which doesn't exist in Linux.  Linux
 * is deeply expecting to scan an emulated PCI configuration
 * space.  So this message is sent here only to drive the state
 * machine on the host forward.
 *
 * Return: 0 on success, -errno on failure
 */
static int hv_send_resources_allocated(struct hv_device *hdev)
{
	struct hv_pcibus_device *hbus = hv_get_drvdata(hdev);
	struct pci_resources_assigned *res_assigned;
	struct pci_resources_assigned2 *res_assigned2;
	struct hv_pci_compl comp_pkt;
	struct hv_pci_dev *hpdev;
	struct pci_packet *pkt;
	size_t size_res;
	int wslot;
	int ret;

	size_res = (hbus->protocol_version < PCI_PROTOCOL_VERSION_1_2)
			? sizeof(*res_assigned) : sizeof(*res_assigned2);

	pkt = kmalloc(sizeof(*pkt) + size_res, GFP_KERNEL);
	if (!pkt)
		return -ENOMEM;

	ret = 0;

	for (wslot = 0; wslot < 256; wslot++) {
		hpdev = get_pcichild_wslot(hbus, wslot);
		if (!hpdev)
			continue;

		memset(pkt, 0, sizeof(*pkt) + size_res);
		init_completion(&comp_pkt.host_event);
		pkt->completion_func = hv_pci_generic_compl;
		pkt->compl_ctxt = &comp_pkt;

		if (hbus->protocol_version < PCI_PROTOCOL_VERSION_1_2) {
			res_assigned =
				(struct pci_resources_assigned *)&pkt->message;
			res_assigned->message_type.type =
				PCI_RESOURCES_ASSIGNED;
			res_assigned->wslot.slot = hpdev->desc.win_slot.slot;
		} else {
			res_assigned2 =
				(struct pci_resources_assigned2 *)&pkt->message;
			res_assigned2->message_type.type =
				PCI_RESOURCES_ASSIGNED2;
			res_assigned2->wslot.slot = hpdev->desc.win_slot.slot;
		}
		put_pcichild(hpdev);

		ret = vmbus_sendpacket(hdev->channel, &pkt->message,
				size_res, (unsigned long)pkt,
				VM_PKT_DATA_INBAND,
				VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
		if (!ret)
			ret = wait_for_response(hdev, &comp_pkt.host_event);
		if (ret)
			break;

		if (comp_pkt.completion_status < 0) {
			ret = -EPROTO;
			dev_err(&hdev->device,
				"resource allocated returned 0x%x",
				comp_pkt.completion_status);
			break;
		}

		hbus->wslot_res_allocated = wslot;
	}

	kfree(pkt);
	return ret;
}

/**
 * hv_send_resources_released() - Report local resources
 * released
 * @hdev:	VMBus's tracking struct for this root PCI bus
 *
 * Return: 0 on success, -errno on failure
 */
static int hv_send_resources_released(struct hv_device *hdev)
{
	struct hv_pcibus_device *hbus = hv_get_drvdata(hdev);
	struct pci_child_message pkt;
	struct hv_pci_dev *hpdev;
	int wslot;
	int ret;

	for (wslot = hbus->wslot_res_allocated; wslot >= 0; wslot--) {
		hpdev = get_pcichild_wslot(hbus, wslot);
		if (!hpdev)
			continue;

		memset(&pkt, 0, sizeof(pkt));
		pkt.message_type.type = PCI_RESOURCES_RELEASED;
		pkt.wslot.slot = hpdev->desc.win_slot.slot;

		put_pcichild(hpdev);

		ret = vmbus_sendpacket(hdev->channel, &pkt, sizeof(pkt), 0,
				       VM_PKT_DATA_INBAND, 0);
		if (ret)
			return ret;

		hbus->wslot_res_allocated = wslot - 1;
	}

	hbus->wslot_res_allocated = -1;

	return 0;
}

static void get_hvpcibus(struct hv_pcibus_device *hbus)
{
	refcount_inc(&hbus->remove_lock);
}

static void put_hvpcibus(struct hv_pcibus_device *hbus)
{
	if (refcount_dec_and_test(&hbus->remove_lock))
		complete(&hbus->remove_event);
}

#define HVPCI_DOM_MAP_SIZE (64 * 1024)
static DECLARE_BITMAP(hvpci_dom_map, HVPCI_DOM_MAP_SIZE);

/*
 * PCI domain number 0 is used by emulated devices on Gen1 VMs, so define 0
 * as invalid for passthrough PCI devices of this driver.
 */
#define HVPCI_DOM_INVALID 0

/**
 * hv_get_dom_num() - Get a valid PCI domain number
 * Check if the PCI domain number is in use, and return another number if
 * it is in use.
 *
 * @dom: Requested domain number
 *
 * return: domain number on success, HVPCI_DOM_INVALID on failure
 */
static u16 hv_get_dom_num(u16 dom)
{
	unsigned int i;

	if (test_and_set_bit(dom, hvpci_dom_map) == 0)
		return dom;

	for_each_clear_bit(i, hvpci_dom_map, HVPCI_DOM_MAP_SIZE) {
		if (test_and_set_bit(i, hvpci_dom_map) == 0)
			return i;
	}

	return HVPCI_DOM_INVALID;
}

/**
 * hv_put_dom_num() - Mark the PCI domain number as free
 * @dom: Domain number to be freed
 */
static void hv_put_dom_num(u16 dom)
{
	clear_bit(dom, hvpci_dom_map);
}

/**
 * hv_pci_probe() - New VMBus channel probe, for a root PCI bus
 * @hdev:	VMBus's tracking struct for this root PCI bus
 * @dev_id:	Identifies the device itself
 *
 * Return: 0 on success, -errno on failure
 */
static int hv_pci_probe(struct hv_device *hdev,
			const struct hv_vmbus_device_id *dev_id)
{
	struct hv_pcibus_device *hbus;
	u16 dom_req, dom;
	char *name;
	bool enter_d0_retry = true;
	int ret;

	/*
	 * hv_pcibus_device contains the hypercall arguments for retargeting in
	 * hv_irq_unmask(). Those must not cross a page boundary.
	 */
	BUILD_BUG_ON(sizeof(*hbus) > HV_HYP_PAGE_SIZE);

	/*
	 * With the recent 59bb47985c1d ("mm, sl[aou]b: guarantee natural
	 * alignment for kmalloc(power-of-two)"), kzalloc() is able to allocate
	 * a 4KB buffer that is guaranteed to be 4KB-aligned. Here the size and
	 * alignment of hbus is important because hbus's field
	 * retarget_msi_interrupt_params must not cross a 4KB page boundary.
	 *
	 * Here we prefer kzalloc to get_zeroed_page(), because a buffer
	 * allocated by the latter is not tracked and scanned by kmemleak, and
	 * hence kmemleak reports the pointer contained in the hbus buffer
	 * (i.e. the hpdev struct, which is created in new_pcichild_device() and
	 * is tracked by hbus->children) as memory leak (false positive).
	 *
	 * If the kernel doesn't have 59bb47985c1d, get_zeroed_page() *must* be
	 * used to allocate the hbus buffer and we can avoid the kmemleak false
	 * positive by using kmemleak_alloc() and kmemleak_free() to ask
	 * kmemleak to track and scan the hbus buffer.
	 */
	hbus = kzalloc(HV_HYP_PAGE_SIZE, GFP_KERNEL);
	if (!hbus)
		return -ENOMEM;
	hbus->state = hv_pcibus_init;
	hbus->wslot_res_allocated = -1;

	/*
	 * The PCI bus "domain" is what is called "segment" in ACPI and other
	 * specs. Pull it from the instance ID, to get something usually
	 * unique. In rare cases of collision, we will find out another number
	 * not in use.
	 *
	 * Note that, since this code only runs in a Hyper-V VM, Hyper-V
	 * together with this guest driver can guarantee that (1) The only
	 * domain used by Gen1 VMs for something that looks like a physical
	 * PCI bus (which is actually emulated by the hypervisor) is domain 0.
	 * (2) There will be no overlap between domains (after fixing possible
	 * collisions) in the same VM.
	 */
	dom_req = hdev->dev_instance.b[5] << 8 | hdev->dev_instance.b[4];
	dom = hv_get_dom_num(dom_req);

	if (dom == HVPCI_DOM_INVALID) {
		dev_err(&hdev->device,
			"Unable to use dom# 0x%hx or other numbers", dom_req);
		ret = -EINVAL;
		goto free_bus;
	}

	if (dom != dom_req)
		dev_info(&hdev->device,
			 "PCI dom# 0x%hx has collision, using 0x%hx",
			 dom_req, dom);

	hbus->sysdata.domain = dom;

	hbus->hdev = hdev;
	refcount_set(&hbus->remove_lock, 1);
	INIT_LIST_HEAD(&hbus->children);
	INIT_LIST_HEAD(&hbus->dr_list);
	INIT_LIST_HEAD(&hbus->resources_for_children);
	spin_lock_init(&hbus->config_lock);
	spin_lock_init(&hbus->device_list_lock);
	spin_lock_init(&hbus->retarget_msi_interrupt_lock);
	init_completion(&hbus->remove_event);
	hbus->wq = alloc_ordered_workqueue("hv_pci_%x", 0,
					   hbus->sysdata.domain);
	if (!hbus->wq) {
		ret = -ENOMEM;
		goto free_dom;
	}

	ret = vmbus_open(hdev->channel, pci_ring_size, pci_ring_size, NULL, 0,
			 hv_pci_onchannelcallback, hbus);
	if (ret)
		goto destroy_wq;

	hv_set_drvdata(hdev, hbus);

	ret = hv_pci_protocol_negotiation(hdev, pci_protocol_versions,
					  ARRAY_SIZE(pci_protocol_versions));
	if (ret)
		goto close;

	ret = hv_allocate_config_window(hbus);
	if (ret)
		goto close;

	hbus->cfg_addr = ioremap(hbus->mem_config->start,
				 PCI_CONFIG_MMIO_LENGTH);
	if (!hbus->cfg_addr) {
		dev_err(&hdev->device,
			"Unable to map a virtual address for config space\n");
		ret = -ENOMEM;
		goto free_config;
	}

	name = kasprintf(GFP_KERNEL, "%pUL", &hdev->dev_instance);
	if (!name) {
		ret = -ENOMEM;
		goto unmap;
	}

	hbus->sysdata.fwnode = irq_domain_alloc_named_fwnode(name);
	kfree(name);
	if (!hbus->sysdata.fwnode) {
		ret = -ENOMEM;
		goto unmap;
	}

	ret = hv_pcie_init_irq_domain(hbus);
	if (ret)
		goto free_fwnode;

retry:
	ret = hv_pci_query_relations(hdev);
	if (ret)
		goto free_irq_domain;

	ret = hv_pci_enter_d0(hdev);
	/*
	 * In certain case (Kdump) the pci device of interest was
	 * not cleanly shut down and resource is still held on host
	 * side, the host could return invalid device status.
	 * We need to explicitly request host to release the resource
	 * and try to enter D0 again.
	 * Since the hv_pci_bus_exit() call releases structures
	 * of all its child devices, we need to start the retry from
	 * hv_pci_query_relations() call, requesting host to send
	 * the synchronous child device relations message before this
	 * information is needed in hv_send_resources_allocated()
	 * call later.
	 */
	if (ret == -EPROTO && enter_d0_retry) {
		enter_d0_retry = false;

		dev_err(&hdev->device, "Retrying D0 Entry\n");

		/*
		 * Hv_pci_bus_exit() calls hv_send_resources_released()
		 * to free up resources of its child devices.
		 * In the kdump kernel we need to set the
		 * wslot_res_allocated to 255 so it scans all child
		 * devices to release resources allocated in the
		 * normal kernel before panic happened.
		 */
		hbus->wslot_res_allocated = 255;
		ret = hv_pci_bus_exit(hdev, true);

		if (ret == 0)
			goto retry;

		dev_err(&hdev->device,
			"Retrying D0 failed with ret %d\n", ret);
	}
	if (ret)
		goto free_irq_domain;

	ret = hv_pci_allocate_bridge_windows(hbus);
	if (ret)
		goto exit_d0;

	ret = hv_send_resources_allocated(hdev);
	if (ret)
		goto free_windows;

	prepopulate_bars(hbus);

	hbus->state = hv_pcibus_probed;

	ret = create_root_hv_pci_bus(hbus);
	if (ret)
		goto free_windows;

	return 0;

free_windows:
	hv_pci_free_bridge_windows(hbus);
exit_d0:
	(void) hv_pci_bus_exit(hdev, true);
free_irq_domain:
	irq_domain_remove(hbus->irq_domain);
free_fwnode:
	irq_domain_free_fwnode(hbus->sysdata.fwnode);
unmap:
	iounmap(hbus->cfg_addr);
free_config:
	hv_free_config_window(hbus);
close:
	vmbus_close(hdev->channel);
destroy_wq:
	destroy_workqueue(hbus->wq);
free_dom:
	hv_put_dom_num(hbus->sysdata.domain);
free_bus:
	kfree(hbus);
	return ret;
}

static int hv_pci_bus_exit(struct hv_device *hdev, bool keep_devs)
{
	struct hv_pcibus_device *hbus = hv_get_drvdata(hdev);
	struct {
		struct pci_packet teardown_packet;
		u8 buffer[sizeof(struct pci_message)];
	} pkt;
	struct hv_pci_compl comp_pkt;
	struct hv_pci_dev *hpdev, *tmp;
	unsigned long flags;
	int ret;

	/*
	 * After the host sends the RESCIND_CHANNEL message, it doesn't
	 * access the per-channel ringbuffer any longer.
	 */
	if (hdev->channel->rescind)
		return 0;

	if (!keep_devs) {
		struct list_head removed;

		/* Move all present children to the list on stack */
		INIT_LIST_HEAD(&removed);
		spin_lock_irqsave(&hbus->device_list_lock, flags);
		list_for_each_entry_safe(hpdev, tmp, &hbus->children, list_entry)
			list_move_tail(&hpdev->list_entry, &removed);
		spin_unlock_irqrestore(&hbus->device_list_lock, flags);

		/* Remove all children in the list */
		list_for_each_entry_safe(hpdev, tmp, &removed, list_entry) {
			list_del(&hpdev->list_entry);
			if (hpdev->pci_slot)
				pci_destroy_slot(hpdev->pci_slot);
			/* For the two refs got in new_pcichild_device() */
			put_pcichild(hpdev);
			put_pcichild(hpdev);
		}
	}

	ret = hv_send_resources_released(hdev);
	if (ret) {
		dev_err(&hdev->device,
			"Couldn't send resources released packet(s)\n");
		return ret;
	}

	memset(&pkt.teardown_packet, 0, sizeof(pkt.teardown_packet));
	init_completion(&comp_pkt.host_event);
	pkt.teardown_packet.completion_func = hv_pci_generic_compl;
	pkt.teardown_packet.compl_ctxt = &comp_pkt;
	pkt.teardown_packet.message[0].type = PCI_BUS_D0EXIT;

	ret = vmbus_sendpacket(hdev->channel, &pkt.teardown_packet.message,
			       sizeof(struct pci_message),
			       (unsigned long)&pkt.teardown_packet,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret)
		return ret;

	if (wait_for_completion_timeout(&comp_pkt.host_event, 10 * HZ) == 0)
		return -ETIMEDOUT;

	return 0;
}

/**
 * hv_pci_remove() - Remove routine for this VMBus channel
 * @hdev:	VMBus's tracking struct for this root PCI bus
 *
 * Return: 0 on success, -errno on failure
 */
static int hv_pci_remove(struct hv_device *hdev)
{
	struct hv_pcibus_device *hbus;
	int ret;

	hbus = hv_get_drvdata(hdev);
	if (hbus->state == hv_pcibus_installed) {
		tasklet_disable(&hdev->channel->callback_event);
		hbus->state = hv_pcibus_removing;
		tasklet_enable(&hdev->channel->callback_event);
		destroy_workqueue(hbus->wq);
		hbus->wq = NULL;
		/*
		 * At this point, no work is running or can be scheduled
		 * on hbus-wq. We can't race with hv_pci_devices_present()
		 * or hv_pci_eject_device(), it's safe to proceed.
		 */

		/* Remove the bus from PCI's point of view. */
		pci_lock_rescan_remove();
		pci_stop_root_bus(hbus->pci_bus);
		hv_pci_remove_slots(hbus);
		pci_remove_root_bus(hbus->pci_bus);
		pci_unlock_rescan_remove();
	}

	ret = hv_pci_bus_exit(hdev, false);

	vmbus_close(hdev->channel);

	iounmap(hbus->cfg_addr);
	hv_free_config_window(hbus);
	pci_free_resource_list(&hbus->resources_for_children);
	hv_pci_free_bridge_windows(hbus);
	irq_domain_remove(hbus->irq_domain);
	irq_domain_free_fwnode(hbus->sysdata.fwnode);
	put_hvpcibus(hbus);
	wait_for_completion(&hbus->remove_event);

	hv_put_dom_num(hbus->sysdata.domain);

	kfree(hbus);
	return ret;
}

static int hv_pci_suspend(struct hv_device *hdev)
{
	struct hv_pcibus_device *hbus = hv_get_drvdata(hdev);
	enum hv_pcibus_state old_state;
	int ret;

	/*
	 * hv_pci_suspend() must make sure there are no pending work items
	 * before calling vmbus_close(), since it runs in a process context
	 * as a callback in dpm_suspend().  When it starts to run, the channel
	 * callback hv_pci_onchannelcallback(), which runs in a tasklet
	 * context, can be still running concurrently and scheduling new work
	 * items onto hbus->wq in hv_pci_devices_present() and
	 * hv_pci_eject_device(), and the work item handlers can access the
	 * vmbus channel, which can be being closed by hv_pci_suspend(), e.g.
	 * the work item handler pci_devices_present_work() ->
	 * new_pcichild_device() writes to the vmbus channel.
	 *
	 * To eliminate the race, hv_pci_suspend() disables the channel
	 * callback tasklet, sets hbus->state to hv_pcibus_removing, and
	 * re-enables the tasklet. This way, when hv_pci_suspend() proceeds,
	 * it knows that no new work item can be scheduled, and then it flushes
	 * hbus->wq and safely closes the vmbus channel.
	 */
	tasklet_disable(&hdev->channel->callback_event);

	/* Change the hbus state to prevent new work items. */
	old_state = hbus->state;
	if (hbus->state == hv_pcibus_installed)
		hbus->state = hv_pcibus_removing;

	tasklet_enable(&hdev->channel->callback_event);

	if (old_state != hv_pcibus_installed)
		return -EINVAL;

	flush_workqueue(hbus->wq);

	ret = hv_pci_bus_exit(hdev, true);
	if (ret)
		return ret;

	vmbus_close(hdev->channel);

	return 0;
}

static int hv_pci_restore_msi_msg(struct pci_dev *pdev, void *arg)
{
	struct msi_desc *entry;
	struct irq_data *irq_data;

	for_each_pci_msi_entry(entry, pdev) {
		irq_data = irq_get_irq_data(entry->irq);
		if (WARN_ON_ONCE(!irq_data))
			return -EINVAL;

		hv_compose_msi_msg(irq_data, &entry->msg);
	}

	return 0;
}

/*
 * Upon resume, pci_restore_msi_state() -> ... ->  __pci_write_msi_msg()
 * directly writes the MSI/MSI-X registers via MMIO, but since Hyper-V
 * doesn't trap and emulate the MMIO accesses, here hv_compose_msi_msg()
 * must be used to ask Hyper-V to re-create the IOMMU Interrupt Remapping
 * Table entries.
 */
static void hv_pci_restore_msi_state(struct hv_pcibus_device *hbus)
{
	pci_walk_bus(hbus->pci_bus, hv_pci_restore_msi_msg, NULL);
}

static int hv_pci_resume(struct hv_device *hdev)
{
	struct hv_pcibus_device *hbus = hv_get_drvdata(hdev);
	enum pci_protocol_version_t version[1];
	int ret;

	hbus->state = hv_pcibus_init;

	ret = vmbus_open(hdev->channel, pci_ring_size, pci_ring_size, NULL, 0,
			 hv_pci_onchannelcallback, hbus);
	if (ret)
		return ret;

	/* Only use the version that was in use before hibernation. */
	version[0] = hbus->protocol_version;
	ret = hv_pci_protocol_negotiation(hdev, version, 1);
	if (ret)
		goto out;

	ret = hv_pci_query_relations(hdev);
	if (ret)
		goto out;

	ret = hv_pci_enter_d0(hdev);
	if (ret)
		goto out;

	ret = hv_send_resources_allocated(hdev);
	if (ret)
		goto out;

	prepopulate_bars(hbus);

	hv_pci_restore_msi_state(hbus);

	hbus->state = hv_pcibus_installed;
	return 0;
out:
	vmbus_close(hdev->channel);
	return ret;
}

static const struct hv_vmbus_device_id hv_pci_id_table[] = {
	/* PCI Pass-through Class ID */
	/* 44C4F61D-4444-4400-9D52-802E27EDE19F */
	{ HV_PCIE_GUID, },
	{ },
};

MODULE_DEVICE_TABLE(vmbus, hv_pci_id_table);

static struct hv_driver hv_pci_drv = {
	.name		= "hv_pci",
	.id_table	= hv_pci_id_table,
	.probe		= hv_pci_probe,
	.remove		= hv_pci_remove,
	.suspend	= hv_pci_suspend,
	.resume		= hv_pci_resume,
};

static void __exit exit_hv_pci_drv(void)
{
	vmbus_driver_unregister(&hv_pci_drv);

	hvpci_block_ops.read_block = NULL;
	hvpci_block_ops.write_block = NULL;
	hvpci_block_ops.reg_blk_invalidate = NULL;
}

static int __init init_hv_pci_drv(void)
{
	if (!hv_is_hyperv_initialized())
		return -ENODEV;

	/* Set the invalid domain number's bit, so it will not be used */
	set_bit(HVPCI_DOM_INVALID, hvpci_dom_map);

	/* Initialize PCI block r/w interface */
	hvpci_block_ops.read_block = hv_read_config_block;
	hvpci_block_ops.write_block = hv_write_config_block;
	hvpci_block_ops.reg_blk_invalidate = hv_register_block_invalidate;

	return vmbus_driver_register(&hv_pci_drv);
}

module_init(init_hv_pci_drv);
module_exit(exit_hv_pci_drv);

MODULE_DESCRIPTION("Hyper-V PCI");
MODULE_LICENSE("GPL v2");
