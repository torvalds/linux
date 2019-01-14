/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/ {
	images {
		fdt@ocelot_pcb123 {
			description = "MSCC Ocelot PCB123 Device Tree";
			data = /incbin/("boot/dts/mscc/ocelot_pcb123.dtb");
			type = "flat_dt";
			arch = "mips";
			compression = "none";
			hash@0 {
				algo = "sha1";
			};
		};

		fdt@ocelot_pcb120 {
			description = "MSCC Ocelot PCB120 Device Tree";
			data = /incbin/("boot/dts/mscc/ocelot_pcb120.dtb");
			type = "flat_dt";
			arch = "mips";
			compression = "none";
			hash@0 {
				algo = "sha1";
			};
		};
	};

	configurations {
		conf@ocelot_pcb123 {
			description = "Ocelot Linux kernel";
			kernel = "kernel@0";
			fdt = "fdt@ocelot_pcb123";
		};

		conf@ocelot_pcb120 {
			description = "Ocelot Linux kernel";
			kernel = "kernel@0";
			fdt = "fdt@ocelot_pcb120";
		};
	};
};
