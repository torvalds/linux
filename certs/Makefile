# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel signature checking certificates.
#

obj-$(CONFIG_SYSTEM_TRUSTED_KEYRING) += system_keyring.o system_certificates.o common.o
obj-$(CONFIG_SYSTEM_BLACKLIST_KEYRING) += blacklist.o common.o
obj-$(CONFIG_SYSTEM_REVOCATION_LIST) += revocation_certificates.o
ifneq ($(CONFIG_SYSTEM_BLACKLIST_HASH_LIST),)
obj-$(CONFIG_SYSTEM_BLACKLIST_KEYRING) += blacklist_hashes.o
else
obj-$(CONFIG_SYSTEM_BLACKLIST_KEYRING) += blacklist_nohashes.o
endif

quiet_cmd_extract_certs  = CERT    $@
      cmd_extract_certs  = $(obj)/extract-cert $(2) $@

$(obj)/system_certificates.o: $(obj)/x509_certificate_list

$(obj)/x509_certificate_list: $(CONFIG_SYSTEM_TRUSTED_KEYS) $(obj)/extract-cert FORCE
	$(call if_changed,extract_certs,$(if $(CONFIG_SYSTEM_TRUSTED_KEYS),$<,""))

targets += x509_certificate_list

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
ifeq ($(CONFIG_MODULE_SIG_KEY),certs/signing_key.pem)

keytype-$(CONFIG_MODULE_SIG_KEY_TYPE_ECDSA) := -newkey ec -pkeyopt ec_paramgen_curve:secp384r1

quiet_cmd_gen_key = GENKEY  $@
      cmd_gen_key = openssl req -new -nodes -utf8 -$(CONFIG_MODULE_SIG_HASH) -days 36500 \
		-batch -x509 -config $< \
		-outform PEM -out $@ -keyout $@ $(keytype-y) 2>&1

$(obj)/signing_key.pem: $(obj)/x509.genkey FORCE
	$(call if_changed,gen_key)

targets += signing_key.pem

quiet_cmd_copy_x509_config = COPY    $@
      cmd_copy_x509_config = cat $(srctree)/$(src)/default_x509.genkey > $@

# You can provide your own config file. If not present, copy the default one.
$(obj)/x509.genkey:
	$(call cmd,copy_x509_config)

endif # CONFIG_MODULE_SIG_KEY

# If CONFIG_MODULE_SIG_KEY isn't a PKCS#11 URI, depend on it
ifneq ($(filter-out pkcs11:%, $(CONFIG_MODULE_SIG_KEY)),)
X509_DEP := $(CONFIG_MODULE_SIG_KEY)
endif

$(obj)/system_certificates.o: $(obj)/signing_key.x509

$(obj)/signing_key.x509: $(X509_DEP) $(obj)/extract-cert FORCE
	$(call if_changed,extract_certs,$(if $(CONFIG_MODULE_SIG_KEY),$(if $(X509_DEP),$<,$(CONFIG_MODULE_SIG_KEY)),""))
endif # CONFIG_MODULE_SIG

targets += signing_key.x509

$(obj)/revocation_certificates.o: $(obj)/x509_revocation_list

$(obj)/x509_revocation_list: $(CONFIG_SYSTEM_REVOCATION_KEYS) $(obj)/extract-cert FORCE
	$(call if_changed,extract_certs,$(if $(CONFIG_SYSTEM_REVOCATION_KEYS),$<,""))

targets += x509_revocation_list

hostprogs := extract-cert

HOSTCFLAGS_extract-cert.o = $(shell pkg-config --cflags libcrypto 2> /dev/null)
HOSTLDLIBS_extract-cert = $(shell pkg-config --libs libcrypto 2> /dev/null || echo -lcrypto)
