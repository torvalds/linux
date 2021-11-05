# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel signature checking certificates.
#

obj-$(CONFIG_SYSTEM_TRUSTED_KEYRING) += system_keyring.o system_certificates.o common.o
obj-$(CONFIG_SYSTEM_BLACKLIST_KEYRING) += blacklist.o common.o
obj-$(CONFIG_SYSTEM_REVOCATION_LIST) += revocation_certificates.o
ifneq ($(CONFIG_SYSTEM_BLACKLIST_HASH_LIST),"")
obj-$(CONFIG_SYSTEM_BLACKLIST_KEYRING) += blacklist_hashes.o
else
obj-$(CONFIG_SYSTEM_BLACKLIST_KEYRING) += blacklist_nohashes.o
endif

ifeq ($(CONFIG_SYSTEM_TRUSTED_KEYRING),y)

$(eval $(call config_filename,SYSTEM_TRUSTED_KEYS))

# GCC doesn't include .incbin files in -MD generated dependencies (PR#66871)
$(obj)/system_certificates.o: $(obj)/x509_certificate_list

# Cope with signing_key.x509 existing in $(srctree) not $(objtree)
AFLAGS_system_certificates.o := -I$(srctree)

quiet_cmd_extract_certs  = EXTRACT_CERTS   $(patsubst "%",%,$(2))
      cmd_extract_certs  = scripts/extract-cert $(2) $@

targets += x509_certificate_list
$(obj)/x509_certificate_list: scripts/extract-cert $(SYSTEM_TRUSTED_KEYS_SRCPREFIX)$(SYSTEM_TRUSTED_KEYS_FILENAME) FORCE
	$(call if_changed,extract_certs,$(SYSTEM_TRUSTED_KEYS_SRCPREFIX)$(CONFIG_SYSTEM_TRUSTED_KEYS))
endif # CONFIG_SYSTEM_TRUSTED_KEYRING

clean-files := x509_certificate_list .x509.list x509_revocation_list

ifeq ($(CONFIG_MODULE_SIG),y)
	SIGN_KEY = y
endif

ifeq ($(CONFIG_IMA_APPRAISE_MODSIG),y)
ifeq ($(CONFIG_MODULES),y)
	SIGN_KEY = y
endif
endif

ifdef SIGN_KEY
###############################################################################
#
# If module signing is requested, say by allyesconfig, but a key has not been
# supplied, then one will need to be generated to make sure the build does not
# fail and that the kernel may be used afterwards.
#
###############################################################################

# We do it this way rather than having a boolean option for enabling an
# external private key, because 'make randconfig' might enable such a
# boolean option and we unfortunately can't make it depend on !RANDCONFIG.
ifeq ($(CONFIG_MODULE_SIG_KEY),"certs/signing_key.pem")

keytype-$(CONFIG_MODULE_SIG_KEY_TYPE_ECDSA) := -newkey ec -pkeyopt ec_paramgen_curve:secp384r1

quiet_cmd_gen_key = GENKEY  $@
      cmd_gen_key = openssl req -new -nodes -utf8 -$(CONFIG_MODULE_SIG_HASH) -days 36500 \
		-batch -x509 -config $(obj)/x509.genkey \
		-outform PEM -out $(obj)/signing_key.pem \
		-keyout $(obj)/signing_key.pem $(keytype-y) 2>&1

$(obj)/signing_key.pem: $(obj)/x509.genkey FORCE
	$(call if_changed,gen_key)

targets += signing_key.pem

quiet_cmd_copy_x509_config = COPY    $@
      cmd_copy_x509_config = cat $(srctree)/$(src)/default_x509.genkey > $@

# You can provide your own config file. If not present, copy the default one.
$(obj)/x509.genkey:
	$(call cmd,copy_x509_config)

endif # CONFIG_MODULE_SIG_KEY

$(eval $(call config_filename,MODULE_SIG_KEY))

# If CONFIG_MODULE_SIG_KEY isn't a PKCS#11 URI, depend on it
ifeq ($(patsubst pkcs11:%,%,$(firstword $(MODULE_SIG_KEY_FILENAME))),$(firstword $(MODULE_SIG_KEY_FILENAME)))
X509_DEP := $(MODULE_SIG_KEY_SRCPREFIX)$(MODULE_SIG_KEY_FILENAME)
endif

# GCC PR#66871 again.
$(obj)/system_certificates.o: $(obj)/signing_key.x509

targets += signing_key.x509
$(obj)/signing_key.x509: scripts/extract-cert $(X509_DEP) FORCE
	$(call if_changed,extract_certs,$(MODULE_SIG_KEY_SRCPREFIX)$(CONFIG_MODULE_SIG_KEY))
endif # CONFIG_MODULE_SIG

ifeq ($(CONFIG_SYSTEM_REVOCATION_LIST),y)

$(eval $(call config_filename,SYSTEM_REVOCATION_KEYS))

$(obj)/revocation_certificates.o: $(obj)/x509_revocation_list

quiet_cmd_extract_certs  = EXTRACT_CERTS   $(patsubst "%",%,$(2))
      cmd_extract_certs  = scripts/extract-cert $(2) $@

targets += x509_revocation_list
$(obj)/x509_revocation_list: scripts/extract-cert $(SYSTEM_REVOCATION_KEYS_SRCPREFIX)$(SYSTEM_REVOCATION_KEYS_FILENAME) FORCE
	$(call if_changed,extract_certs,$(SYSTEM_REVOCATION_KEYS_SRCPREFIX)$(CONFIG_SYSTEM_REVOCATION_KEYS))
endif
