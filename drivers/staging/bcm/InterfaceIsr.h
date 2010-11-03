#ifndef _INTERFACE_ISR_H
#define _INTERFACE_ISR_H

int CreateInterruptUrb(PS_INTERFACE_ADAPTER psIntfAdapter);


INT StartInterruptUrb(PS_INTERFACE_ADAPTER psIntfAdapter);


VOID InterfaceEnableInterrupt(PMINI_ADAPTER Adapter);

VOID InterfaceDisableInterrupt(PMINI_ADAPTER Adapter);

#endif

