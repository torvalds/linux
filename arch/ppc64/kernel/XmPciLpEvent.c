/*
 * File XmPciLpEvent.h created by Wayne Holm on Mon Jan 15 2001.
 *
 * This module handles PCI interrupt events sent by the iSeries Hypervisor.
*/

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/bootmem.h>
#include <linux/ide.h>

#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/HvCallPci.h>
#include <asm/iSeries/XmPciLpEvent.h>
#include <asm/ppcdebug.h>

static long Pci_Interrupt_Count;
static long Pci_Event_Count;

enum XmPciLpEvent_Subtype {
	XmPciLpEvent_BusCreated	   = 0,		// PHB has been created
	XmPciLpEvent_BusError	   = 1,		// PHB has failed
	XmPciLpEvent_BusFailed	   = 2,		// Msg to Secondary, Primary failed bus
	XmPciLpEvent_NodeFailed	   = 4,		// Multi-adapter bridge has failed
	XmPciLpEvent_NodeRecovered = 5,		// Multi-adapter bridge has recovered
	XmPciLpEvent_BusRecovered  = 12,	// PHB has been recovered
	XmPciLpEvent_UnQuiesceBus  = 18,	// Secondary bus unqiescing
	XmPciLpEvent_BridgeError   = 21,	// Bridge Error
	XmPciLpEvent_SlotInterrupt = 22		// Slot interrupt
};

struct XmPciLpEvent_BusInterrupt {
	HvBusNumber	busNumber;
	HvSubBusNumber	subBusNumber;
};

struct XmPciLpEvent_NodeInterrupt {
	HvBusNumber	busNumber;
	HvSubBusNumber	subBusNumber;
	HvAgentId	deviceId;
};

struct XmPciLpEvent {
	struct HvLpEvent hvLpEvent;

	union {
		u64 alignData;			// Align on an 8-byte boundary

		struct {
			u32		fisr;
			HvBusNumber	busNumber;
			HvSubBusNumber	subBusNumber;
			HvAgentId	deviceId;
		} slotInterrupt;

		struct XmPciLpEvent_BusInterrupt busFailed;
		struct XmPciLpEvent_BusInterrupt busRecovered;
		struct XmPciLpEvent_BusInterrupt busCreated;

		struct XmPciLpEvent_NodeInterrupt nodeFailed;
		struct XmPciLpEvent_NodeInterrupt nodeRecovered;

	} eventData;

};

static void intReceived(struct XmPciLpEvent *eventParm,
		struct pt_regs *regsParm);

static void XmPciLpEvent_handler(struct HvLpEvent *eventParm,
		struct pt_regs *regsParm)
{
#ifdef CONFIG_PCI
#if 0
	PPCDBG(PPCDBG_BUSWALK, "XmPciLpEvent_handler, type 0x%x\n",
			eventParm->xType);
#endif
	++Pci_Event_Count;

	if (eventParm && (eventParm->xType == HvLpEvent_Type_PciIo)) {
		switch (eventParm->xFlags.xFunction) {
		case HvLpEvent_Function_Int:
			intReceived((struct XmPciLpEvent *)eventParm, regsParm);
			break;
		case HvLpEvent_Function_Ack:
			printk(KERN_ERR
				"XmPciLpEvent.c: unexpected ack received\n");
			break;
		default:
			printk(KERN_ERR
				"XmPciLpEvent.c: unexpected event function %d\n",
				(int)eventParm->xFlags.xFunction);
			break;
		}
	} else if (eventParm)
		printk(KERN_ERR
			"XmPciLpEvent.c: Unrecognized PCI event type 0x%x\n",
			(int)eventParm->xType);
	else
		printk(KERN_ERR "XmPciLpEvent.c: NULL event received\n");
#endif
}

static void intReceived(struct XmPciLpEvent *eventParm,
		struct pt_regs *regsParm)
{
	int irq;

	++Pci_Interrupt_Count;
#if 0
	PPCDBG(PPCDBG_BUSWALK, "PCI: XmPciLpEvent.c: intReceived\n");
#endif

	switch (eventParm->hvLpEvent.xSubtype) {
	case XmPciLpEvent_SlotInterrupt:
		irq = eventParm->hvLpEvent.xCorrelationToken;
		/* Dispatch the interrupt handlers for this irq */
		ppc_irq_dispatch_handler(regsParm, irq);
		HvCallPci_eoi(eventParm->eventData.slotInterrupt.busNumber,
			eventParm->eventData.slotInterrupt.subBusNumber,
			eventParm->eventData.slotInterrupt.deviceId);
		break;
		/* Ignore error recovery events for now */
	case XmPciLpEvent_BusCreated:
		printk(KERN_INFO "XmPciLpEvent.c: system bus %d created\n",
			eventParm->eventData.busCreated.busNumber);
		break;
	case XmPciLpEvent_BusError:
	case XmPciLpEvent_BusFailed:
		printk(KERN_INFO "XmPciLpEvent.c: system bus %d failed\n",
			eventParm->eventData.busFailed.busNumber);
		break;
	case XmPciLpEvent_BusRecovered:
	case XmPciLpEvent_UnQuiesceBus:
		printk(KERN_INFO "XmPciLpEvent.c: system bus %d recovered\n",
			eventParm->eventData.busRecovered.busNumber);
		break;
	case XmPciLpEvent_NodeFailed:
	case XmPciLpEvent_BridgeError:
		printk(KERN_INFO
			"XmPciLpEvent.c: multi-adapter bridge %d/%d/%d failed\n",
			eventParm->eventData.nodeFailed.busNumber,
			eventParm->eventData.nodeFailed.subBusNumber,
			eventParm->eventData.nodeFailed.deviceId);
		break;
	case XmPciLpEvent_NodeRecovered:
		printk(KERN_INFO
			"XmPciLpEvent.c: multi-adapter bridge %d/%d/%d recovered\n",
			eventParm->eventData.nodeRecovered.busNumber,
			eventParm->eventData.nodeRecovered.subBusNumber,
			eventParm->eventData.nodeRecovered.deviceId);
		break;
	default:
		printk(KERN_ERR
			"XmPciLpEvent.c: unrecognized event subtype 0x%x\n",
			eventParm->hvLpEvent.xSubtype);
		break;
	}
}


/* This should be called sometime prior to buswalk (init_IRQ would be good) */
int XmPciLpEvent_init()
{
	int xRc;

	PPCDBG(PPCDBG_BUSWALK,
			"XmPciLpEvent_init, Register Event type 0x%04X\n",
			HvLpEvent_Type_PciIo);

	xRc = HvLpEvent_registerHandler(HvLpEvent_Type_PciIo,
			&XmPciLpEvent_handler);
	if (xRc == 0) {
		xRc = HvLpEvent_openPath(HvLpEvent_Type_PciIo, 0);
		if (xRc != 0)
			printk(KERN_ERR
				"XmPciLpEvent.c: open event path failed with rc 0x%x\n",
				xRc);
	} else
		printk(KERN_ERR
			"XmPciLpEvent.c: register handler failed with rc 0x%x\n",
			xRc);
	return xRc;
}
