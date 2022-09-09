# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel signature checking certificates.
#

obj-$(CONFIG_SYSTEM_TRUSTED_KEYRING) += system_keyring.o system_certificates.o
obj-$(CONFIG_SYSTEM_BLACKLIST_KEYRING) += blacklist.o blacklist_hashes.o
obj-$(CONFIG_SYSTEM_REVOCATION_LIST) += revocation_certificates.o

$(obj)/blacklist_hashes.o: $(obj)/blacklist_hash_list
CFLAGS_blacklist_hashes.o := -I $(obj)

quiet_cmd_check_and_copy_blacklist_hash_list = GEN     $@
      cmd_check_and_copy_blacklist_hash_list = \
	$(if $(CONFIG_SYSTEM_BLACKLIST_HASH_LIST), \
	$(AWK) -f $(srctree)/$(src)/check-blacklist-hashes.awk $(CONFIG_SYSTEM_BLACKLIST_HASH_LIST) >&2; \
	{ cat $(CONFIG_SYSTEM_BLACKLIST_HASH_LIST); echo $(comma) NULL; } > $@, \
	echo NULL > $@)

$(obj)/blacklist_hash_list: $(CONFIG_SYSTEM_BLACKLIST_HASH_LIST) FORCE
	$(call if_changed,check_and_copy_blacklist_hash_list)

targets += blacklist_hash_list

quiet_cmd_extract_certs  = CERT    $@
      cmd_extract_certs  = $(obj)/extract-cert $(extract-cert-in) $@
extract-cert-in = $(or $(filter-out $(obj)/extract-cert, $(real-prereqs)),"")

$(obj)/system_certificates.o: $(obj)/x509_certificate_list

$(obj)/x509_certificate_list: $(CONFIG_SYSTEM_TRUSTED_KEYS) $(obj)/extract-cert FORCE
	$(call if_changed,extract_certs)

targets += x509_certificate_list

# If module signing is requested, say by allyesconfig, but a key has not been
# supplied, then one will need to be generated to make sure the build does not
# fail and that the kernel may be used afterwards.
#
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

$(obj)/system_certificates.o: $(obj)/signing_key.x509

PKCS11_URI := $(filter pkcs11:%, $(CONFIG_MODULE_SIG_KEY))
ifdef PKCS11_URI
$(obj)/signing_key.x509: extract-cert-in := $(PKCS11_URI)
endif

$(obj)/signing_key.x509: $(filter-out $(PKCS11_URI),$(CONFIG_MODULE_SIG_KEY)) $(obj)/extract-cert FORCE
	$(call if_changed,extract_certs)

targets += signing_key.x509

$(obj)/revocation_certificates.o: $(obj)/x509_revocation_list

$(obj)/x509_revocation_list: $(CONFIG_SYSTEM_REVOCATION_KEYS) $(obj)/extract-cert FORCE
	$(call if_changed,extract_certs)

targets += x509_revocation_list

hostprogs := extract-cert

HOSTCFLAGS_extract-cert.o = $(shell $(HOSTPKG_CONFIG) --cflags libcrypto 2> /dev/null)
HOSTLDLIBS_extract-cert = $(shell $(HOSTPKG_CONFIG) --libs libcrypto 2> /dev/null || echo -lcrypto)
