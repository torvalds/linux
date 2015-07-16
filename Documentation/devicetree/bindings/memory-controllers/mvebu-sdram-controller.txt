Device Tree bindings for MVEBU SDRAM controllers

The Marvell EBU SoCs all have a SDRAM controller. The SDRAM controller
differs from one SoC variant to another, but they also share a number
of commonalities.

For now, this Device Tree binding documentation only documents the
Armada XP SDRAM controller.

Required properties:

 - compatible: for Armada XP, "marvell,armada-xp-sdram-controller"
 - reg: a resource specifier for the register space, which should
   include all SDRAM controller registers as per the datasheet.

Example:

sdramc@1400 {
	compatible = "marvell,armada-xp-sdram-controller";
	reg = <0x1400 0x500>;
};
