#ifndef _INTERFACE_ISR_H
#define _INTERFACE_ISR_H

int CreateInterruptUrb(PS_INTERFACE_ADAPTER psIntfAdapter);


INT StartInterruptUrb(PS_INTERFACE_ADAPTER psIntfAdapter);


VOID InterfaceEnableInterrupt(struct bcm_mini_adapter *Adapter);

VOID InterfaceDisableInterrupt(struct bcm_mini_adapter *Adapter);

#endif

