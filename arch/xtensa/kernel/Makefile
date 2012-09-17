#
# Makefile for the Linux/Xtensa kernel.
#

extra-y := head.o vmlinux.lds

obj-y := align.o entry.o irq.o coprocessor.o process.o ptrace.o \
	 setup.o signal.o syscall.o time.o traps.o vectors.o platform.o  \
	 pci-dma.o

obj-$(CONFIG_KGDB) += xtensa-stub.o
obj-$(CONFIG_PCI) += pci.o
obj-$(CONFIG_MODULES) += xtensa_ksyms.o module.o

# In the Xtensa architecture, assembly generates literals which must always
# precede the L32R instruction with a relative offset less than 256 kB.
# Therefore, the .text and .literal section must be combined in parenthesis
# in the linker script, such as: *(.literal .text).
#
# We need to post-process the generated vmlinux.lds scripts to convert
# *(xxx.text) to  *(xxx.literal xxx.text) for the following text sections:
#   .text .ref.text .*init.text .*exit.text .text.*
#
# Replicate rules in scripts/Makefile.build

sed-y = -e 's/\*(\(\.[a-z]*it\|\.ref\|\)\.text)/*(\1.literal \1.text)/g'    \
	-e 's/\.text\.unlikely/.literal.unlikely .text.unlikely/g' \
	-e 's/\*(\(\.text\.[a-z]*\))/*(\1.literal \1)/g'

quiet_cmd__cpp_lds_S = LDS     $@
      cmd__cpp_lds_S = $(CPP) $(cpp_flags) -P -C -Uxtensa -D__ASSEMBLY__ $< \
                       | sed $(sed-y) >$@

$(obj)/vmlinux.lds: $(src)/vmlinux.lds.S FORCE
	$(call if_changed_dep,_cpp_lds_S)
