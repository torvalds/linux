BCM2836 per-CPU interrupt controller

The BCM2836 has a per-cpu interrupt controller for the timer, PMU
events, and SMP IPIs.  One of the CPUs may receive interrupts for the
peripheral (GPU) events, which chain to the BCM2835-style interrupt
controller.

Required properties:

- compatible:	 	Should be "brcm,bcm2836-l1-intc"
- reg:			Specifies base physical address and size of the
			  registers
- interrupt-controller:	Identifies the node as an interrupt controller
- #interrupt-cells:	Specifies the number of cells needed to encode an
			  interrupt source. The value shall be 2

Please refer to interrupts.txt in this directory for details of the common
Interrupt Controllers bindings used by client devices.

The interrupt sources are as follows:

0: CNTPSIRQ
1: CNTPNSIRQ
2: CNTHPIRQ
3: CNTVIRQ
8: GPU_FAST
9: PMU_FAST

Example:

local_intc: local_intc {
	compatible = "brcm,bcm2836-l1-intc";
	reg = <0x40000000 0x100>;
	interrupt-controller;
	#interrupt-cells = <2>;
	interrupt-parent = <&local_intc>;
};
