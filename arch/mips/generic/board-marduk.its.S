/ {
	images {
		fdt-marduk {
			description = "img,pistachio-marduk Device Tree";
			data = /incbin/("boot/dts/img/pistachio_marduk.dtb");
			type = "flat_dt";
			arch = "mips";
			compression = "none";
			hash {
				algo = "sha1";
			};
		};
	};

	configurations {
		conf-marduk {
			description = "Marduk Linux kernel";
			kernel = "kernel";
			fdt = "fdt-marduk";
		};
	};
};
