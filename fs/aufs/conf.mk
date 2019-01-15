# SPDX-License-Identifier: GPL-2.0

AuConfStr = CONFIG_AUFS_FS=${CONFIG_AUFS_FS}

define AuConf
ifdef ${1}
AuConfStr += ${1}=${${1}}
endif
endef

AuConfAll = BRANCH_MAX_127 BRANCH_MAX_511 BRANCH_MAX_1023 BRANCH_MAX_32767 \
	SBILIST \
	HNOTIFY HFSNOTIFY \
	EXPORT INO_T_64 \
	XATTR \
	FHSM \
	RDU \
	DIRREN \
	SHWH \
	BR_RAMFS \
	BR_FUSE POLL \
	BR_HFSPLUS \
	BDEV_LOOP \
	DEBUG MAGIC_SYSRQ
$(foreach i, ${AuConfAll}, \
	$(eval $(call AuConf,CONFIG_AUFS_${i})))

AuConfName = ${obj}/conf.str
${AuConfName}.tmp: FORCE
	@echo ${AuConfStr} | tr ' ' '\n' | sed -e 's/^/"/' -e 's/$$/\\n"/' > $@
${AuConfName}: ${AuConfName}.tmp
	@diff -q $< $@ > /dev/null 2>&1 || { \
	echo '  GEN    ' $@; \
	cp -p $< $@; \
	}
FORCE:
clean-files += ${AuConfName} ${AuConfName}.tmp
${obj}/sysfs.o: ${AuConfName}

-include ${srctree}/${src}/conf_priv.mk
