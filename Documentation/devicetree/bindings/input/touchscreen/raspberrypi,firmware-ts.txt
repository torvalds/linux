Raspberry Pi firmware based 7" touchscreen
=====================================

Required properties:
 - compatible: "raspberrypi,firmware-ts"

Optional properties:
 - firmware: Reference to RPi's firmware device node
 - touchscreen-size-x: See touchscreen.txt
 - touchscreen-size-y: See touchscreen.txt
 - touchscreen-inverted-x: See touchscreen.txt
 - touchscreen-inverted-y: See touchscreen.txt
 - touchscreen-swapped-x-y: See touchscreen.txt

Example:

firmware: firmware-rpi {
	compatible = "raspberrypi,bcm2835-firmware";
	mboxes = <&mailbox>;

	ts: touchscreen {
		compatible = "raspberrypi,firmware-ts";
		touchscreen-size-x = <800>;
		touchscreen-size-y = <480>;
	};
};
