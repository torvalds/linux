subdir		:= standby

obj-y		+= pm.o standby.o

$(obj)/standby.o: $(obj)/standby/standby.bin

$(obj)/standby/standby.bin: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/standby
