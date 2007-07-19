#
# Makefile for the kernel software RAID and LVM drivers.
#

dm-mod-objs	:= dm.o dm-table.o dm-target.o dm-linear.o dm-stripe.o \
		   dm-ioctl.o dm-io.o kcopyd.o
dm-multipath-objs := dm-hw-handler.o dm-path-selector.o dm-mpath.o
dm-snapshot-objs := dm-snap.o dm-exception-store.o
dm-mirror-objs	:= dm-log.o dm-raid1.o
dm-rdac-objs	:= dm-mpath-rdac.o
md-mod-objs     := md.o bitmap.o
raid456-objs	:= raid5.o raid6algos.o raid6recov.o raid6tables.o \
		   raid6int1.o raid6int2.o raid6int4.o \
		   raid6int8.o raid6int16.o raid6int32.o \
		   raid6altivec1.o raid6altivec2.o raid6altivec4.o \
		   raid6altivec8.o \
		   raid6mmx.o raid6sse1.o raid6sse2.o
hostprogs-y	:= mktables

# Note: link order is important.  All raid personalities
# and must come before md.o, as they each initialise 
# themselves, and md.o may use the personalities when it 
# auto-initialised.

obj-$(CONFIG_MD_LINEAR)		+= linear.o
obj-$(CONFIG_MD_RAID0)		+= raid0.o
obj-$(CONFIG_MD_RAID1)		+= raid1.o
obj-$(CONFIG_MD_RAID10)		+= raid10.o
obj-$(CONFIG_MD_RAID456)	+= raid456.o
obj-$(CONFIG_MD_MULTIPATH)	+= multipath.o
obj-$(CONFIG_MD_FAULTY)		+= faulty.o
obj-$(CONFIG_BLK_DEV_MD)	+= md-mod.o
obj-$(CONFIG_BLK_DEV_DM)	+= dm-mod.o
obj-$(CONFIG_DM_CRYPT)		+= dm-crypt.o
obj-$(CONFIG_DM_DELAY)		+= dm-delay.o
obj-$(CONFIG_DM_MULTIPATH)	+= dm-multipath.o dm-round-robin.o
obj-$(CONFIG_DM_MULTIPATH_EMC)	+= dm-emc.o
obj-$(CONFIG_DM_MULTIPATH_RDAC)	+= dm-rdac.o
obj-$(CONFIG_DM_SNAPSHOT)	+= dm-snapshot.o
obj-$(CONFIG_DM_MIRROR)		+= dm-mirror.o
obj-$(CONFIG_DM_ZERO)		+= dm-zero.o

quiet_cmd_unroll = UNROLL  $@
      cmd_unroll = $(PERL) $(srctree)/$(src)/unroll.pl $(UNROLL) \
                   < $< > $@ || ( rm -f $@ && exit 1 )

ifeq ($(CONFIG_ALTIVEC),y)
altivec_flags := -maltivec -mabi=altivec
endif

targets += raid6int1.c
$(obj)/raid6int1.c:   UNROLL := 1
$(obj)/raid6int1.c:   $(src)/raid6int.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

targets += raid6int2.c
$(obj)/raid6int2.c:   UNROLL := 2
$(obj)/raid6int2.c:   $(src)/raid6int.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

targets += raid6int4.c
$(obj)/raid6int4.c:   UNROLL := 4
$(obj)/raid6int4.c:   $(src)/raid6int.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

targets += raid6int8.c
$(obj)/raid6int8.c:   UNROLL := 8
$(obj)/raid6int8.c:   $(src)/raid6int.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

targets += raid6int16.c
$(obj)/raid6int16.c:  UNROLL := 16
$(obj)/raid6int16.c:  $(src)/raid6int.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

targets += raid6int32.c
$(obj)/raid6int32.c:  UNROLL := 32
$(obj)/raid6int32.c:  $(src)/raid6int.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

CFLAGS_raid6altivec1.o += $(altivec_flags)
targets += raid6altivec1.c
$(obj)/raid6altivec1.c:   UNROLL := 1
$(obj)/raid6altivec1.c:   $(src)/raid6altivec.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

CFLAGS_raid6altivec2.o += $(altivec_flags)
targets += raid6altivec2.c
$(obj)/raid6altivec2.c:   UNROLL := 2
$(obj)/raid6altivec2.c:   $(src)/raid6altivec.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

CFLAGS_raid6altivec4.o += $(altivec_flags)
targets += raid6altivec4.c
$(obj)/raid6altivec4.c:   UNROLL := 4
$(obj)/raid6altivec4.c:   $(src)/raid6altivec.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

CFLAGS_raid6altivec8.o += $(altivec_flags)
targets += raid6altivec8.c
$(obj)/raid6altivec8.c:   UNROLL := 8
$(obj)/raid6altivec8.c:   $(src)/raid6altivec.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

quiet_cmd_mktable = TABLE   $@
      cmd_mktable = $(obj)/mktables > $@ || ( rm -f $@ && exit 1 )

targets += raid6tables.c
$(obj)/raid6tables.c: $(obj)/mktables FORCE
	$(call if_changed,mktable)
