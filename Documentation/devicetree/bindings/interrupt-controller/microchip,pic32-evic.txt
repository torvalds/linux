Microchip PIC32 Interrupt Controller
====================================

The Microchip PIC32 contains an Enhanced Vectored Interrupt Controller (EVIC).
It handles all internal and external interrupts. This controller exists outside
of the CPU and is the arbitrator of all interrupts (including interrupts from
the CPU itself) before they are presented to the CPU.

External interrupts have a software configurable edge polarity. Non external
interrupts have a type and polarity that is determined by the source of the
interrupt.

Required properties
-------------------

- compatible: Should be "microchip,pic32mzda-evic"
- reg: Specifies physical base address and size of register range.
- interrupt-controller: Identifies the node as an interrupt controller.
- #interrupt cells: Specifies the number of cells used to encode an interrupt
  source connected to this controller. The value shall be 2 and interrupt
  descriptor shall have the following format:

	<hw_irq irq_type>

  hw_irq - represents the hardware interrupt number as in the data sheet.
  irq_type - is used to describe the type and polarity of an interrupt. For
  internal interrupts use IRQ_TYPE_EDGE_RISING for non persistent interrupts and
  IRQ_TYPE_LEVEL_HIGH for persistent interrupts. For external interrupts use
  IRQ_TYPE_EDGE_RISING or IRQ_TYPE_EDGE_FALLING to select the desired polarity.

Optional properties
-------------------
- microchip,external-irqs: u32 array of external interrupts with software
  polarity configuration. This array corresponds to the bits in the INTCON
  SFR.

Example
-------

evic: interrupt-controller@1f810000 {
	compatible = "microchip,pic32mzda-evic";
	interrupt-controller;
	#interrupt-cells = <2>;
	reg = <0x1f810000 0x1000>;
	microchip,external-irqs = <3 8 13 18 23>;
};

Each device/peripheral must request its interrupt line with the associated type
and polarity.

Internal interrupt DTS snippet
------------------------------

device@1f800000 {
	...
	interrupts = <113 IRQ_TYPE_LEVEL_HIGH>;
	...
};

External interrupt DTS snippet
------------------------------

device@1f800000 {
	...
	interrupts = <3 IRQ_TYPE_EDGE_RISING>;
	...
};
