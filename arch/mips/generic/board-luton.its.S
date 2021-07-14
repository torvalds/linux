/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/ {
	images {
		fdt-luton_pcb091 {
			description = "MSCC Luton PCB091 Device Tree";
			data = /incbin/("boot/dts/mscc/luton_pcb091.dtb");
			type = "flat_dt";
			arch = "mips";
			compression = "none";
			hash {
				algo = "sha1";
			};
		};
	};

	configurations {
		pcb091 {
			description = "Luton Linux kernel";
			kernel = "kernel";
			fdt = "fdt-luton_pcb091";
		};
	};
};
