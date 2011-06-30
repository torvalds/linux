* Freescale Enhanced Secure Digital Host Controller (eSDHC) for i.MX

The Enhanced Secure Digital Host Controller on Freescale i.MX family
provides an interface for MMC, SD, and SDIO types of memory cards.

Required properties:
- compatible : Should be "fsl,<chip>-esdhc"
- reg : Should contain eSDHC registers location and length
- interrupts : Should contain eSDHC interrupt

Optional properties:
- fsl,card-wired : Indicate the card is wired to host permanently
- fsl,cd-internal : Indicate to use controller internal card detection
- fsl,wp-internal : Indicate to use controller internal write protection
- cd-gpios : Specify GPIOs for card detection
- wp-gpios : Specify GPIOs for write protection

Examples:

esdhc@70004000 {
	compatible = "fsl,imx51-esdhc";
	reg = <0x70004000 0x4000>;
	interrupts = <1>;
	fsl,cd-internal;
	fsl,wp-internal;
};

esdhc@70008000 {
	compatible = "fsl,imx51-esdhc";
	reg = <0x70008000 0x4000>;
	interrupts = <2>;
	cd-gpios = <&gpio0 6 0>; /* GPIO1_6 */
	wp-gpios = <&gpio0 5 0>; /* GPIO1_5 */
};
