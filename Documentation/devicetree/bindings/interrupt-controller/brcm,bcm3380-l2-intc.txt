Broadcom BCM3380-style Level 1 / Level 2 interrupt controller

This interrupt controller shows up in various forms on many BCM338x/BCM63xx
chipsets.  It has the following properties:

- outputs a single interrupt signal to its interrupt controller parent

- contains one or more enable/status word pairs, which often appear at
  different offsets in different blocks

- no atomic set/clear operations

Required properties:

- compatible: should be "brcm,bcm3380-l2-intc"
- reg: specifies one or more enable/status pairs, in the following format:
  <enable_reg 0x4 status_reg 0x4>...
- interrupt-controller: identifies the node as an interrupt controller
- #interrupt-cells: specifies the number of cells needed to encode an interrupt
  source, should be 1.
- interrupts: specifies the interrupt line in the interrupt-parent controller
  node, valid values depend on the type of parent interrupt controller

Optional properties:

- brcm,irq-can-wake: if present, this means the L2 controller can be used as a
  wakeup source for system suspend/resume.

Example:

irq0_intc: interrupt-controller@10000020 {
	compatible = "brcm,bcm3380-l2-intc";
	reg = <0x10000024 0x4 0x1000002c 0x4>,
	      <0x10000020 0x4 0x10000028 0x4>;
	interrupt-controller;
	#interrupt-cells = <1>;
	interrupt-parent = <&cpu_intc>;
	interrupts = <2>;
};
