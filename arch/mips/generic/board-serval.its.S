/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/ {
	images {
		fdt@serval_pcb105 {
			description = "MSCC Serval PCB105 Device Tree";
			data = /incbin/("boot/dts/mscc/serval_pcb105.dtb");
			type = "flat_dt";
			arch = "mips";
			compression = "none";
			hash@0 {
				algo = "sha1";
			};
		};
	};

	configurations {
		pcb105 {
			description = "Serval Linux kernel";
			kernel = "kernel@0";
			fdt = "fdt@serval_pcb105";
			ramdisk = "ramdisk";
		};
	};
};
