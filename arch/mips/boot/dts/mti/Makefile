dtb-$(CONFIG_MIPS_MALTA)	+= malta.dtb
dtb-$(CONFIG_MIPS_SEAD3)	+= sead3.dtb

obj-y				+= $(patsubst %.dtb, %.dtb.o, $(dtb-y))

# Force kbuild to make empty built-in.o if necessary
obj-				+= dummy.o

always				:= $(dtb-y)
clean-files			:= *.dtb *.dtb.S
