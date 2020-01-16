/ {
	images {
		fdt@xilfpga {
			description = "MIPSfpga (xilfpga) Device Tree";
			data = /incbin/("boot/dts/xilfpga/nexys4ddr.dtb");
			type = "flat_dt";
			arch = "mips";
			compression = "yesne";
			hash@0 {
				algo = "sha1";
			};
		};
	};

	configurations {
		conf@xilfpga {
			description = "MIPSfpga Linux kernel";
			kernel = "kernel@0";
			fdt = "fdt@xilfpga";
		};
	};
};
