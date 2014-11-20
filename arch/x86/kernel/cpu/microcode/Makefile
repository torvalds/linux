microcode-y				:= core.o
obj-$(CONFIG_MICROCODE)			+= microcode.o
microcode-$(CONFIG_MICROCODE_INTEL)	+= intel.o intel_lib.o
microcode-$(CONFIG_MICROCODE_AMD)	+= amd.o
obj-$(CONFIG_MICROCODE_EARLY)		+= core_early.o
obj-$(CONFIG_MICROCODE_INTEL_EARLY)	+= intel_early.o
obj-$(CONFIG_MICROCODE_AMD_EARLY)	+= amd_early.o
