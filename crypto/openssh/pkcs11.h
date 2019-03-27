/* $OpenBSD: pkcs11.h,v 1.3 2013/11/26 19:15:09 deraadt Exp $ */
/* pkcs11.h
   Copyright 2006, 2007 g10 Code GmbH
   Copyright 2006 Andreas Jellinghaus

   This file is free software; as a special exception the author gives
   unlimited permission to copy and/or distribute it, with or without
   modifications, as long as this notice is preserved.

   This file is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY, to the extent permitted by law; without even
   the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
   PURPOSE.  */

/* Please submit changes back to the Scute project at
   http://www.scute.org/ (or send them to marcus@g10code.com), so that
   they can be picked up by other projects from there as well.  */

/* This file is a modified implementation of the PKCS #11 standard by
   RSA Security Inc.  It is mostly a drop-in replacement, with the
   following change:

   This header file does not require any macro definitions by the user
   (like CK_DEFINE_FUNCTION etc).  In fact, it defines those macros
   for you (if useful, some are missing, let me know if you need
   more).

   There is an additional API available that does comply better to the
   GNU coding standard.  It can be switched on by defining
   CRYPTOKI_GNU before including this header file.  For this, the
   following changes are made to the specification:

   All structure types are changed to a "struct ck_foo" where CK_FOO
   is the type name in PKCS #11.

   All non-structure types are changed to ck_foo_t where CK_FOO is the
   lowercase version of the type name in PKCS #11.  The basic types
   (CK_ULONG et al.) are removed without substitute.

   All members of structures are modified in the following way: Type
   indication prefixes are removed, and underscore characters are
   inserted before words.  Then the result is lowercased.

   Note that function names are still in the original case, as they
   need for ABI compatibility.

   CK_FALSE, CK_TRUE and NULL_PTR are removed without substitute.  Use
   <stdbool.h>.

   If CRYPTOKI_COMPAT is defined before including this header file,
   then none of the API changes above take place, and the API is the
   one defined by the PKCS #11 standard.  */

#ifndef PKCS11_H
#define PKCS11_H 1

#if defined(__cplusplus)
extern "C" {
#endif


/* The version of cryptoki we implement.  The revision is changed with
   each modification of this file.  If you do not use the "official"
   version of this file, please consider deleting the revision macro
   (you may use a macro with a different name to keep track of your
   versions).  */
#define CRYPTOKI_VERSION_MAJOR		2
#define CRYPTOKI_VERSION_MINOR		20
#define CRYPTOKI_VERSION_REVISION	6


/* Compatibility interface is default, unless CRYPTOKI_GNU is
   given.  */
#ifndef CRYPTOKI_GNU
#ifndef CRYPTOKI_COMPAT
#define CRYPTOKI_COMPAT 1
#endif
#endif

/* System dependencies.  */

#if defined(_WIN32) || defined(CRYPTOKI_FORCE_WIN32)

/* There is a matching pop below.  */
#pragma pack(push, cryptoki, 1)

#ifdef CRYPTOKI_EXPORTS
#define CK_SPEC __declspec(dllexport)
#else
#define CK_SPEC __declspec(dllimport)
#endif

#else

#define CK_SPEC

#endif


#ifdef CRYPTOKI_COMPAT
  /* If we are in compatibility mode, switch all exposed names to the
     PKCS #11 variant.  There are corresponding #undefs below.  */

#define ck_flags_t CK_FLAGS
#define ck_version _CK_VERSION

#define ck_info _CK_INFO
#define cryptoki_version cryptokiVersion
#define manufacturer_id manufacturerID
#define library_description libraryDescription
#define library_version libraryVersion

#define ck_notification_t CK_NOTIFICATION
#define ck_slot_id_t CK_SLOT_ID

#define ck_slot_info _CK_SLOT_INFO
#define slot_description slotDescription
#define hardware_version hardwareVersion
#define firmware_version firmwareVersion

#define ck_token_info _CK_TOKEN_INFO
#define serial_number serialNumber
#define max_session_count ulMaxSessionCount
#define session_count ulSessionCount
#define max_rw_session_count ulMaxRwSessionCount
#define rw_session_count ulRwSessionCount
#define max_pin_len ulMaxPinLen
#define min_pin_len ulMinPinLen
#define total_public_memory ulTotalPublicMemory
#define free_public_memory ulFreePublicMemory
#define total_private_memory ulTotalPrivateMemory
#define free_private_memory ulFreePrivateMemory
#define utc_time utcTime

#define ck_session_handle_t CK_SESSION_HANDLE
#define ck_user_type_t CK_USER_TYPE
#define ck_state_t CK_STATE

#define ck_session_info _CK_SESSION_INFO
#define slot_id slotID
#define device_error ulDeviceError

#define ck_object_handle_t CK_OBJECT_HANDLE
#define ck_object_class_t CK_OBJECT_CLASS
#define ck_hw_feature_type_t CK_HW_FEATURE_TYPE
#define ck_key_type_t CK_KEY_TYPE
#define ck_certificate_type_t CK_CERTIFICATE_TYPE
#define ck_attribute_type_t CK_ATTRIBUTE_TYPE

#define ck_attribute _CK_ATTRIBUTE
#define value pValue
#define value_len ulValueLen

#define ck_date _CK_DATE

#define ck_mechanism_type_t CK_MECHANISM_TYPE

#define ck_mechanism _CK_MECHANISM
#define parameter pParameter
#define parameter_len ulParameterLen

#define ck_mechanism_info _CK_MECHANISM_INFO
#define min_key_size ulMinKeySize
#define max_key_size ulMaxKeySize

#define ck_rv_t CK_RV
#define ck_notify_t CK_NOTIFY

#define ck_function_list _CK_FUNCTION_LIST

#define ck_createmutex_t CK_CREATEMUTEX
#define ck_destroymutex_t CK_DESTROYMUTEX
#define ck_lockmutex_t CK_LOCKMUTEX
#define ck_unlockmutex_t CK_UNLOCKMUTEX

#define ck_c_initialize_args _CK_C_INITIALIZE_ARGS
#define create_mutex CreateMutex
#define destroy_mutex DestroyMutex
#define lock_mutex LockMutex
#define unlock_mutex UnlockMutex
#define reserved pReserved

#endif	/* CRYPTOKI_COMPAT */



typedef unsigned long ck_flags_t;

struct ck_version
{
  unsigned char major;
  unsigned char minor;
};


struct ck_info
{
  struct ck_version cryptoki_version;
  unsigned char manufacturer_id[32];
  ck_flags_t flags;
  unsigned char library_description[32];
  struct ck_version library_version;
};


typedef unsigned long ck_notification_t;

#define CKN_SURRENDER	(0)


typedef unsigned long ck_slot_id_t;


struct ck_slot_info
{
  unsigned char slot_description[64];
  unsigned char manufacturer_id[32];
  ck_flags_t flags;
  struct ck_version hardware_version;
  struct ck_version firmware_version;
};


#define CKF_TOKEN_PRESENT	(1 << 0)
#define CKF_REMOVABLE_DEVICE	(1 << 1)
#define CKF_HW_SLOT		(1 << 2)
#define CKF_ARRAY_ATTRIBUTE	(1 << 30)


struct ck_token_info
{
  unsigned char label[32];
  unsigned char manufacturer_id[32];
  unsigned char model[16];
  unsigned char serial_number[16];
  ck_flags_t flags;
  unsigned long max_session_count;
  unsigned long session_count;
  unsigned long max_rw_session_count;
  unsigned long rw_session_count;
  unsigned long max_pin_len;
  unsigned long min_pin_len;
  unsigned long total_public_memory;
  unsigned long free_public_memory;
  unsigned long total_private_memory;
  unsigned long free_private_memory;
  struct ck_version hardware_version;
  struct ck_version firmware_version;
  unsigned char utc_time[16];
};


#define CKF_RNG					(1 << 0)
#define CKF_WRITE_PROTECTED			(1 << 1)
#define CKF_LOGIN_REQUIRED			(1 << 2)
#define CKF_USER_PIN_INITIALIZED		(1 << 3)
#define CKF_RESTORE_KEY_NOT_NEEDED		(1 << 5)
#define CKF_CLOCK_ON_TOKEN			(1 << 6)
#define CKF_PROTECTED_AUTHENTICATION_PATH	(1 << 8)
#define CKF_DUAL_CRYPTO_OPERATIONS		(1 << 9)
#define CKF_TOKEN_INITIALIZED			(1 << 10)
#define CKF_SECONDARY_AUTHENTICATION		(1 << 11)
#define CKF_USER_PIN_COUNT_LOW			(1 << 16)
#define CKF_USER_PIN_FINAL_TRY			(1 << 17)
#define CKF_USER_PIN_LOCKED			(1 << 18)
#define CKF_USER_PIN_TO_BE_CHANGED		(1 << 19)
#define CKF_SO_PIN_COUNT_LOW			(1 << 20)
#define CKF_SO_PIN_FINAL_TRY			(1 << 21)
#define CKF_SO_PIN_LOCKED			(1 << 22)
#define CKF_SO_PIN_TO_BE_CHANGED		(1 << 23)

#define CK_UNAVAILABLE_INFORMATION	((unsigned long) -1)
#define CK_EFFECTIVELY_INFINITE		(0)


typedef unsigned long ck_session_handle_t;

#define CK_INVALID_HANDLE	(0)


typedef unsigned long ck_user_type_t;

#define CKU_SO			(0)
#define CKU_USER		(1)
#define CKU_CONTEXT_SPECIFIC	(2)


typedef unsigned long ck_state_t;

#define CKS_RO_PUBLIC_SESSION	(0)
#define CKS_RO_USER_FUNCTIONS	(1)
#define CKS_RW_PUBLIC_SESSION	(2)
#define CKS_RW_USER_FUNCTIONS	(3)
#define CKS_RW_SO_FUNCTIONS	(4)


struct ck_session_info
{
  ck_slot_id_t slot_id;
  ck_state_t state;
  ck_flags_t flags;
  unsigned long device_error;
};

#define CKF_RW_SESSION		(1 << 1)
#define CKF_SERIAL_SESSION	(1 << 2)


typedef unsigned long ck_object_handle_t;


typedef unsigned long ck_object_class_t;

#define CKO_DATA		(0)
#define CKO_CERTIFICATE		(1)
#define CKO_PUBLIC_KEY		(2)
#define CKO_PRIVATE_KEY		(3)
#define CKO_SECRET_KEY		(4)
#define CKO_HW_FEATURE		(5)
#define CKO_DOMAIN_PARAMETERS	(6)
#define CKO_MECHANISM		(7)
#define CKO_VENDOR_DEFINED	(1U << 31)


typedef unsigned long ck_hw_feature_type_t;

#define CKH_MONOTONIC_COUNTER	(1)
#define CKH_CLOCK		(2)
#define CKH_USER_INTERFACE	(3)
#define CKH_VENDOR_DEFINED	(1U << 31)


typedef unsigned long ck_key_type_t;

#define CKK_RSA			(0)
#define CKK_DSA			(1)
#define CKK_DH			(2)
#define CKK_ECDSA		(3)
#define CKK_EC			(3)
#define CKK_X9_42_DH		(4)
#define CKK_KEA			(5)
#define CKK_GENERIC_SECRET	(0x10)
#define CKK_RC2			(0x11)
#define CKK_RC4			(0x12)
#define CKK_DES			(0x13)
#define CKK_DES2		(0x14)
#define CKK_DES3		(0x15)
#define CKK_CAST		(0x16)
#define CKK_CAST3		(0x17)
#define CKK_CAST128		(0x18)
#define CKK_RC5			(0x19)
#define CKK_IDEA		(0x1a)
#define CKK_SKIPJACK		(0x1b)
#define CKK_BATON		(0x1c)
#define CKK_JUNIPER		(0x1d)
#define CKK_CDMF		(0x1e)
#define CKK_AES			(0x1f)
#define CKK_BLOWFISH		(0x20)
#define CKK_TWOFISH		(0x21)
#define CKK_VENDOR_DEFINED	(1U << 31)

typedef unsigned long ck_certificate_type_t;

#define CKC_X_509		(0)
#define CKC_X_509_ATTR_CERT	(1)
#define CKC_WTLS		(2)
#define CKC_VENDOR_DEFINED	(1U << 31)


typedef unsigned long ck_attribute_type_t;

#define CKA_CLASS			(0)
#define CKA_TOKEN			(1)
#define CKA_PRIVATE			(2)
#define CKA_LABEL			(3)
#define CKA_APPLICATION			(0x10)
#define CKA_VALUE			(0x11)
#define CKA_OBJECT_ID			(0x12)
#define CKA_CERTIFICATE_TYPE		(0x80)
#define CKA_ISSUER			(0x81)
#define CKA_SERIAL_NUMBER		(0x82)
#define CKA_AC_ISSUER			(0x83)
#define CKA_OWNER			(0x84)
#define CKA_ATTR_TYPES			(0x85)
#define CKA_TRUSTED			(0x86)
#define CKA_CERTIFICATE_CATEGORY	(0x87)
#define CKA_JAVA_MIDP_SECURITY_DOMAIN	(0x88)
#define CKA_URL				(0x89)
#define CKA_HASH_OF_SUBJECT_PUBLIC_KEY	(0x8a)
#define CKA_HASH_OF_ISSUER_PUBLIC_KEY	(0x8b)
#define CKA_CHECK_VALUE			(0x90)
#define CKA_KEY_TYPE			(0x100)
#define CKA_SUBJECT			(0x101)
#define CKA_ID				(0x102)
#define CKA_SENSITIVE			(0x103)
#define CKA_ENCRYPT			(0x104)
#define CKA_DECRYPT			(0x105)
#define CKA_WRAP			(0x106)
#define CKA_UNWRAP			(0x107)
#define CKA_SIGN			(0x108)
#define CKA_SIGN_RECOVER		(0x109)
#define CKA_VERIFY			(0x10a)
#define CKA_VERIFY_RECOVER		(0x10b)
#define CKA_DERIVE			(0x10c)
#define CKA_START_DATE			(0x110)
#define CKA_END_DATE			(0x111)
#define CKA_MODULUS			(0x120)
#define CKA_MODULUS_BITS		(0x121)
#define CKA_PUBLIC_EXPONENT		(0x122)
#define CKA_PRIVATE_EXPONENT		(0x123)
#define CKA_PRIME_1			(0x124)
#define CKA_PRIME_2			(0x125)
#define CKA_EXPONENT_1			(0x126)
#define CKA_EXPONENT_2			(0x127)
#define CKA_COEFFICIENT			(0x128)
#define CKA_PRIME			(0x130)
#define CKA_SUBPRIME			(0x131)
#define CKA_BASE			(0x132)
#define CKA_PRIME_BITS			(0x133)
#define CKA_SUB_PRIME_BITS		(0x134)
#define CKA_VALUE_BITS			(0x160)
#define CKA_VALUE_LEN			(0x161)
#define CKA_EXTRACTABLE			(0x162)
#define CKA_LOCAL			(0x163)
#define CKA_NEVER_EXTRACTABLE		(0x164)
#define CKA_ALWAYS_SENSITIVE		(0x165)
#define CKA_KEY_GEN_MECHANISM		(0x166)
#define CKA_MODIFIABLE			(0x170)
#define CKA_ECDSA_PARAMS		(0x180)
#define CKA_EC_PARAMS			(0x180)
#define CKA_EC_POINT			(0x181)
#define CKA_SECONDARY_AUTH		(0x200)
#define CKA_AUTH_PIN_FLAGS		(0x201)
#define CKA_ALWAYS_AUTHENTICATE		(0x202)
#define CKA_WRAP_WITH_TRUSTED		(0x210)
#define CKA_HW_FEATURE_TYPE		(0x300)
#define CKA_RESET_ON_INIT		(0x301)
#define CKA_HAS_RESET			(0x302)
#define CKA_PIXEL_X			(0x400)
#define CKA_PIXEL_Y			(0x401)
#define CKA_RESOLUTION			(0x402)
#define CKA_CHAR_ROWS			(0x403)
#define CKA_CHAR_COLUMNS		(0x404)
#define CKA_COLOR			(0x405)
#define CKA_BITS_PER_PIXEL		(0x406)
#define CKA_CHAR_SETS			(0x480)
#define CKA_ENCODING_METHODS		(0x481)
#define CKA_MIME_TYPES			(0x482)
#define CKA_MECHANISM_TYPE		(0x500)
#define CKA_REQUIRED_CMS_ATTRIBUTES	(0x501)
#define CKA_DEFAULT_CMS_ATTRIBUTES	(0x502)
#define CKA_SUPPORTED_CMS_ATTRIBUTES	(0x503)
#define CKA_WRAP_TEMPLATE		(CKF_ARRAY_ATTRIBUTE | 0x211)
#define CKA_UNWRAP_TEMPLATE		(CKF_ARRAY_ATTRIBUTE | 0x212)
#define CKA_ALLOWED_MECHANISMS		(CKF_ARRAY_ATTRIBUTE | 0x600)
#define CKA_VENDOR_DEFINED		(1U << 31)


struct ck_attribute
{
  ck_attribute_type_t type;
  void *value;
  unsigned long value_len;
};


struct ck_date
{
  unsigned char year[4];
  unsigned char month[2];
  unsigned char day[2];
};


typedef unsigned long ck_mechanism_type_t;

#define CKM_RSA_PKCS_KEY_PAIR_GEN	(0)
#define CKM_RSA_PKCS			(1)
#define CKM_RSA_9796			(2)
#define CKM_RSA_X_509			(3)
#define CKM_MD2_RSA_PKCS		(4)
#define CKM_MD5_RSA_PKCS		(5)
#define CKM_SHA1_RSA_PKCS		(6)
#define CKM_RIPEMD128_RSA_PKCS		(7)
#define CKM_RIPEMD160_RSA_PKCS		(8)
#define CKM_RSA_PKCS_OAEP		(9)
#define CKM_RSA_X9_31_KEY_PAIR_GEN	(0xa)
#define CKM_RSA_X9_31			(0xb)
#define CKM_SHA1_RSA_X9_31		(0xc)
#define CKM_RSA_PKCS_PSS		(0xd)
#define CKM_SHA1_RSA_PKCS_PSS		(0xe)
#define CKM_DSA_KEY_PAIR_GEN		(0x10)
#define	CKM_DSA				(0x11)
#define CKM_DSA_SHA1			(0x12)
#define CKM_DH_PKCS_KEY_PAIR_GEN	(0x20)
#define CKM_DH_PKCS_DERIVE		(0x21)
#define	CKM_X9_42_DH_KEY_PAIR_GEN	(0x30)
#define CKM_X9_42_DH_DERIVE		(0x31)
#define CKM_X9_42_DH_HYBRID_DERIVE	(0x32)
#define CKM_X9_42_MQV_DERIVE		(0x33)
#define CKM_SHA256_RSA_PKCS		(0x40)
#define CKM_SHA384_RSA_PKCS		(0x41)
#define CKM_SHA512_RSA_PKCS		(0x42)
#define CKM_SHA256_RSA_PKCS_PSS		(0x43)
#define CKM_SHA384_RSA_PKCS_PSS		(0x44)
#define CKM_SHA512_RSA_PKCS_PSS		(0x45)
#define CKM_RC2_KEY_GEN			(0x100)
#define CKM_RC2_ECB			(0x101)
#define	CKM_RC2_CBC			(0x102)
#define	CKM_RC2_MAC			(0x103)
#define CKM_RC2_MAC_GENERAL		(0x104)
#define CKM_RC2_CBC_PAD			(0x105)
#define CKM_RC4_KEY_GEN			(0x110)
#define CKM_RC4				(0x111)
#define CKM_DES_KEY_GEN			(0x120)
#define CKM_DES_ECB			(0x121)
#define CKM_DES_CBC			(0x122)
#define CKM_DES_MAC			(0x123)
#define CKM_DES_MAC_GENERAL		(0x124)
#define CKM_DES_CBC_PAD			(0x125)
#define CKM_DES2_KEY_GEN		(0x130)
#define CKM_DES3_KEY_GEN		(0x131)
#define CKM_DES3_ECB			(0x132)
#define CKM_DES3_CBC			(0x133)
#define CKM_DES3_MAC			(0x134)
#define CKM_DES3_MAC_GENERAL		(0x135)
#define CKM_DES3_CBC_PAD		(0x136)
#define CKM_CDMF_KEY_GEN		(0x140)
#define CKM_CDMF_ECB			(0x141)
#define CKM_CDMF_CBC			(0x142)
#define CKM_CDMF_MAC			(0x143)
#define CKM_CDMF_MAC_GENERAL		(0x144)
#define CKM_CDMF_CBC_PAD		(0x145)
#define CKM_MD2				(0x200)
#define CKM_MD2_HMAC			(0x201)
#define CKM_MD2_HMAC_GENERAL		(0x202)
#define CKM_MD5				(0x210)
#define CKM_MD5_HMAC			(0x211)
#define CKM_MD5_HMAC_GENERAL		(0x212)
#define CKM_SHA_1			(0x220)
#define CKM_SHA_1_HMAC			(0x221)
#define CKM_SHA_1_HMAC_GENERAL		(0x222)
#define CKM_RIPEMD128			(0x230)
#define CKM_RIPEMD128_HMAC		(0x231)
#define CKM_RIPEMD128_HMAC_GENERAL	(0x232)
#define CKM_RIPEMD160			(0x240)
#define CKM_RIPEMD160_HMAC		(0x241)
#define CKM_RIPEMD160_HMAC_GENERAL	(0x242)
#define CKM_SHA256			(0x250)
#define CKM_SHA256_HMAC			(0x251)
#define CKM_SHA256_HMAC_GENERAL		(0x252)
#define CKM_SHA384			(0x260)
#define CKM_SHA384_HMAC			(0x261)
#define CKM_SHA384_HMAC_GENERAL		(0x262)
#define CKM_SHA512			(0x270)
#define CKM_SHA512_HMAC			(0x271)
#define CKM_SHA512_HMAC_GENERAL		(0x272)
#define CKM_CAST_KEY_GEN		(0x300)
#define CKM_CAST_ECB			(0x301)
#define CKM_CAST_CBC			(0x302)
#define CKM_CAST_MAC			(0x303)
#define CKM_CAST_MAC_GENERAL		(0x304)
#define CKM_CAST_CBC_PAD		(0x305)
#define CKM_CAST3_KEY_GEN		(0x310)
#define CKM_CAST3_ECB			(0x311)
#define CKM_CAST3_CBC			(0x312)
#define CKM_CAST3_MAC			(0x313)
#define CKM_CAST3_MAC_GENERAL		(0x314)
#define CKM_CAST3_CBC_PAD		(0x315)
#define CKM_CAST5_KEY_GEN		(0x320)
#define CKM_CAST128_KEY_GEN		(0x320)
#define CKM_CAST5_ECB			(0x321)
#define CKM_CAST128_ECB			(0x321)
#define CKM_CAST5_CBC			(0x322)
#define CKM_CAST128_CBC			(0x322)
#define CKM_CAST5_MAC			(0x323)
#define	CKM_CAST128_MAC			(0x323)
#define CKM_CAST5_MAC_GENERAL		(0x324)
#define CKM_CAST128_MAC_GENERAL		(0x324)
#define CKM_CAST5_CBC_PAD		(0x325)
#define CKM_CAST128_CBC_PAD		(0x325)
#define CKM_RC5_KEY_GEN			(0x330)
#define CKM_RC5_ECB			(0x331)
#define CKM_RC5_CBC			(0x332)
#define CKM_RC5_MAC			(0x333)
#define CKM_RC5_MAC_GENERAL		(0x334)
#define CKM_RC5_CBC_PAD			(0x335)
#define CKM_IDEA_KEY_GEN		(0x340)
#define CKM_IDEA_ECB			(0x341)
#define	CKM_IDEA_CBC			(0x342)
#define CKM_IDEA_MAC			(0x343)
#define CKM_IDEA_MAC_GENERAL		(0x344)
#define CKM_IDEA_CBC_PAD		(0x345)
#define CKM_GENERIC_SECRET_KEY_GEN	(0x350)
#define CKM_CONCATENATE_BASE_AND_KEY	(0x360)
#define CKM_CONCATENATE_BASE_AND_DATA	(0x362)
#define CKM_CONCATENATE_DATA_AND_BASE	(0x363)
#define CKM_XOR_BASE_AND_DATA		(0x364)
#define CKM_EXTRACT_KEY_FROM_KEY	(0x365)
#define CKM_SSL3_PRE_MASTER_KEY_GEN	(0x370)
#define CKM_SSL3_MASTER_KEY_DERIVE	(0x371)
#define CKM_SSL3_KEY_AND_MAC_DERIVE	(0x372)
#define CKM_SSL3_MASTER_KEY_DERIVE_DH	(0x373)
#define CKM_TLS_PRE_MASTER_KEY_GEN	(0x374)
#define CKM_TLS_MASTER_KEY_DERIVE	(0x375)
#define CKM_TLS_KEY_AND_MAC_DERIVE	(0x376)
#define CKM_TLS_MASTER_KEY_DERIVE_DH	(0x377)
#define CKM_SSL3_MD5_MAC		(0x380)
#define CKM_SSL3_SHA1_MAC		(0x381)
#define CKM_MD5_KEY_DERIVATION		(0x390)
#define CKM_MD2_KEY_DERIVATION		(0x391)
#define CKM_SHA1_KEY_DERIVATION		(0x392)
#define CKM_PBE_MD2_DES_CBC		(0x3a0)
#define CKM_PBE_MD5_DES_CBC		(0x3a1)
#define CKM_PBE_MD5_CAST_CBC		(0x3a2)
#define CKM_PBE_MD5_CAST3_CBC		(0x3a3)
#define CKM_PBE_MD5_CAST5_CBC		(0x3a4)
#define CKM_PBE_MD5_CAST128_CBC		(0x3a4)
#define CKM_PBE_SHA1_CAST5_CBC		(0x3a5)
#define CKM_PBE_SHA1_CAST128_CBC	(0x3a5)
#define CKM_PBE_SHA1_RC4_128		(0x3a6)
#define CKM_PBE_SHA1_RC4_40		(0x3a7)
#define CKM_PBE_SHA1_DES3_EDE_CBC	(0x3a8)
#define CKM_PBE_SHA1_DES2_EDE_CBC	(0x3a9)
#define CKM_PBE_SHA1_RC2_128_CBC	(0x3aa)
#define CKM_PBE_SHA1_RC2_40_CBC		(0x3ab)
#define CKM_PKCS5_PBKD2			(0x3b0)
#define CKM_PBA_SHA1_WITH_SHA1_HMAC	(0x3c0)
#define CKM_KEY_WRAP_LYNKS		(0x400)
#define CKM_KEY_WRAP_SET_OAEP		(0x401)
#define CKM_SKIPJACK_KEY_GEN		(0x1000)
#define CKM_SKIPJACK_ECB64		(0x1001)
#define CKM_SKIPJACK_CBC64		(0x1002)
#define CKM_SKIPJACK_OFB64		(0x1003)
#define CKM_SKIPJACK_CFB64		(0x1004)
#define CKM_SKIPJACK_CFB32		(0x1005)
#define CKM_SKIPJACK_CFB16		(0x1006)
#define CKM_SKIPJACK_CFB8		(0x1007)
#define CKM_SKIPJACK_WRAP		(0x1008)
#define CKM_SKIPJACK_PRIVATE_WRAP	(0x1009)
#define CKM_SKIPJACK_RELAYX		(0x100a)
#define CKM_KEA_KEY_PAIR_GEN		(0x1010)
#define CKM_KEA_KEY_DERIVE		(0x1011)
#define CKM_FORTEZZA_TIMESTAMP		(0x1020)
#define CKM_BATON_KEY_GEN		(0x1030)
#define CKM_BATON_ECB128		(0x1031)
#define CKM_BATON_ECB96			(0x1032)
#define CKM_BATON_CBC128		(0x1033)
#define CKM_BATON_COUNTER		(0x1034)
#define CKM_BATON_SHUFFLE		(0x1035)
#define CKM_BATON_WRAP			(0x1036)
#define CKM_ECDSA_KEY_PAIR_GEN		(0x1040)
#define CKM_EC_KEY_PAIR_GEN		(0x1040)
#define CKM_ECDSA			(0x1041)
#define CKM_ECDSA_SHA1			(0x1042)
#define CKM_ECDH1_DERIVE		(0x1050)
#define CKM_ECDH1_COFACTOR_DERIVE	(0x1051)
#define CKM_ECMQV_DERIVE		(0x1052)
#define CKM_JUNIPER_KEY_GEN		(0x1060)
#define CKM_JUNIPER_ECB128		(0x1061)
#define CKM_JUNIPER_CBC128		(0x1062)
#define CKM_JUNIPER_COUNTER		(0x1063)
#define CKM_JUNIPER_SHUFFLE		(0x1064)
#define CKM_JUNIPER_WRAP		(0x1065)
#define CKM_FASTHASH			(0x1070)
#define CKM_AES_KEY_GEN			(0x1080)
#define CKM_AES_ECB			(0x1081)
#define CKM_AES_CBC			(0x1082)
#define CKM_AES_MAC			(0x1083)
#define CKM_AES_MAC_GENERAL		(0x1084)
#define CKM_AES_CBC_PAD			(0x1085)
#define CKM_DSA_PARAMETER_GEN		(0x2000)
#define CKM_DH_PKCS_PARAMETER_GEN	(0x2001)
#define CKM_X9_42_DH_PARAMETER_GEN	(0x2002)
#define CKM_VENDOR_DEFINED		(1U << 31)


struct ck_mechanism
{
  ck_mechanism_type_t mechanism;
  void *parameter;
  unsigned long parameter_len;
};


struct ck_mechanism_info
{
  unsigned long min_key_size;
  unsigned long max_key_size;
  ck_flags_t flags;
};

#define CKF_HW			(1 << 0)
#define CKF_ENCRYPT		(1 << 8)
#define CKF_DECRYPT		(1 << 9)
#define CKF_DIGEST		(1 << 10)
#define CKF_SIGN		(1 << 11)
#define CKF_SIGN_RECOVER	(1 << 12)
#define CKF_VERIFY		(1 << 13)
#define CKF_VERIFY_RECOVER	(1 << 14)
#define CKF_GENERATE		(1 << 15)
#define CKF_GENERATE_KEY_PAIR	(1 << 16)
#define CKF_WRAP		(1 << 17)
#define CKF_UNWRAP		(1 << 18)
#define CKF_DERIVE		(1 << 19)
#define CKF_EXTENSION		(1U << 31)


/* Flags for C_WaitForSlotEvent.  */
#define CKF_DONT_BLOCK				(1)


typedef unsigned long ck_rv_t;


typedef ck_rv_t (*ck_notify_t) (ck_session_handle_t session,
				ck_notification_t event, void *application);

/* Forward reference.  */
struct ck_function_list;

#define _CK_DECLARE_FUNCTION(name, args)	\
typedef ck_rv_t (*CK_ ## name) args;		\
ck_rv_t CK_SPEC name args

_CK_DECLARE_FUNCTION (C_Initialize, (void *init_args));
_CK_DECLARE_FUNCTION (C_Finalize, (void *reserved));
_CK_DECLARE_FUNCTION (C_GetInfo, (struct ck_info *info));
_CK_DECLARE_FUNCTION (C_GetFunctionList,
		      (struct ck_function_list **function_list));

_CK_DECLARE_FUNCTION (C_GetSlotList,
		      (unsigned char token_present, ck_slot_id_t *slot_list,
		       unsigned long *count));
_CK_DECLARE_FUNCTION (C_GetSlotInfo,
		      (ck_slot_id_t slot_id, struct ck_slot_info *info));
_CK_DECLARE_FUNCTION (C_GetTokenInfo,
		      (ck_slot_id_t slot_id, struct ck_token_info *info));
_CK_DECLARE_FUNCTION (C_WaitForSlotEvent,
		      (ck_flags_t flags, ck_slot_id_t *slot, void *reserved));
_CK_DECLARE_FUNCTION (C_GetMechanismList,
		      (ck_slot_id_t slot_id,
		       ck_mechanism_type_t *mechanism_list,
		       unsigned long *count));
_CK_DECLARE_FUNCTION (C_GetMechanismInfo,
		      (ck_slot_id_t slot_id, ck_mechanism_type_t type,
		       struct ck_mechanism_info *info));
_CK_DECLARE_FUNCTION (C_InitToken,
		      (ck_slot_id_t slot_id, unsigned char *pin,
		       unsigned long pin_len, unsigned char *label));
_CK_DECLARE_FUNCTION (C_InitPIN,
		      (ck_session_handle_t session, unsigned char *pin,
		       unsigned long pin_len));
_CK_DECLARE_FUNCTION (C_SetPIN,
		      (ck_session_handle_t session, unsigned char *old_pin,
		       unsigned long old_len, unsigned char *new_pin,
		       unsigned long new_len));

_CK_DECLARE_FUNCTION (C_OpenSession,
		      (ck_slot_id_t slot_id, ck_flags_t flags,
		       void *application, ck_notify_t notify,
		       ck_session_handle_t *session));
_CK_DECLARE_FUNCTION (C_CloseSession, (ck_session_handle_t session));
_CK_DECLARE_FUNCTION (C_CloseAllSessions, (ck_slot_id_t slot_id));
_CK_DECLARE_FUNCTION (C_GetSessionInfo,
		      (ck_session_handle_t session,
		       struct ck_session_info *info));
_CK_DECLARE_FUNCTION (C_GetOperationState,
		      (ck_session_handle_t session,
		       unsigned char *operation_state,
		       unsigned long *operation_state_len));
_CK_DECLARE_FUNCTION (C_SetOperationState,
		      (ck_session_handle_t session,
		       unsigned char *operation_state,
		       unsigned long operation_state_len,
		       ck_object_handle_t encryption_key,
		       ck_object_handle_t authentiation_key));
_CK_DECLARE_FUNCTION (C_Login,
		      (ck_session_handle_t session, ck_user_type_t user_type,
		       unsigned char *pin, unsigned long pin_len));
_CK_DECLARE_FUNCTION (C_Logout, (ck_session_handle_t session));

_CK_DECLARE_FUNCTION (C_CreateObject,
		      (ck_session_handle_t session,
		       struct ck_attribute *templ,
		       unsigned long count, ck_object_handle_t *object));
_CK_DECLARE_FUNCTION (C_CopyObject,
		      (ck_session_handle_t session, ck_object_handle_t object,
		       struct ck_attribute *templ, unsigned long count,
		       ck_object_handle_t *new_object));
_CK_DECLARE_FUNCTION (C_DestroyObject,
		      (ck_session_handle_t session,
		       ck_object_handle_t object));
_CK_DECLARE_FUNCTION (C_GetObjectSize,
		      (ck_session_handle_t session,
		       ck_object_handle_t object,
		       unsigned long *size));
_CK_DECLARE_FUNCTION (C_GetAttributeValue,
		      (ck_session_handle_t session,
		       ck_object_handle_t object,
		       struct ck_attribute *templ,
		       unsigned long count));
_CK_DECLARE_FUNCTION (C_SetAttributeValue,
		      (ck_session_handle_t session,
		       ck_object_handle_t object,
		       struct ck_attribute *templ,
		       unsigned long count));
_CK_DECLARE_FUNCTION (C_FindObjectsInit,
		      (ck_session_handle_t session,
		       struct ck_attribute *templ,
		       unsigned long count));
_CK_DECLARE_FUNCTION (C_FindObjects,
		      (ck_session_handle_t session,
		       ck_object_handle_t *object,
		       unsigned long max_object_count,
		       unsigned long *object_count));
_CK_DECLARE_FUNCTION (C_FindObjectsFinal,
		      (ck_session_handle_t session));

_CK_DECLARE_FUNCTION (C_EncryptInit,
		      (ck_session_handle_t session,
		       struct ck_mechanism *mechanism,
		       ck_object_handle_t key));
_CK_DECLARE_FUNCTION (C_Encrypt,
		      (ck_session_handle_t session,
		       unsigned char *data, unsigned long data_len,
		       unsigned char *encrypted_data,
		       unsigned long *encrypted_data_len));
_CK_DECLARE_FUNCTION (C_EncryptUpdate,
		      (ck_session_handle_t session,
		       unsigned char *part, unsigned long part_len,
		       unsigned char *encrypted_part,
		       unsigned long *encrypted_part_len));
_CK_DECLARE_FUNCTION (C_EncryptFinal,
		      (ck_session_handle_t session,
		       unsigned char *last_encrypted_part,
		       unsigned long *last_encrypted_part_len));

_CK_DECLARE_FUNCTION (C_DecryptInit,
		      (ck_session_handle_t session,
		       struct ck_mechanism *mechanism,
		       ck_object_handle_t key));
_CK_DECLARE_FUNCTION (C_Decrypt,
		      (ck_session_handle_t session,
		       unsigned char *encrypted_data,
		       unsigned long encrypted_data_len,
		       unsigned char *data, unsigned long *data_len));
_CK_DECLARE_FUNCTION (C_DecryptUpdate,
		      (ck_session_handle_t session,
		       unsigned char *encrypted_part,
		       unsigned long encrypted_part_len,
		       unsigned char *part, unsigned long *part_len));
_CK_DECLARE_FUNCTION (C_DecryptFinal,
		      (ck_session_handle_t session,
		       unsigned char *last_part,
		       unsigned long *last_part_len));

_CK_DECLARE_FUNCTION (C_DigestInit,
		      (ck_session_handle_t session,
		       struct ck_mechanism *mechanism));
_CK_DECLARE_FUNCTION (C_Digest,
		      (ck_session_handle_t session,
		       unsigned char *data, unsigned long data_len,
		       unsigned char *digest,
		       unsigned long *digest_len));
_CK_DECLARE_FUNCTION (C_DigestUpdate,
		      (ck_session_handle_t session,
		       unsigned char *part, unsigned long part_len));
_CK_DECLARE_FUNCTION (C_DigestKey,
		      (ck_session_handle_t session, ck_object_handle_t key));
_CK_DECLARE_FUNCTION (C_DigestFinal,
		      (ck_session_handle_t session,
		       unsigned char *digest,
		       unsigned long *digest_len));

_CK_DECLARE_FUNCTION (C_SignInit,
		      (ck_session_handle_t session,
		       struct ck_mechanism *mechanism,
		       ck_object_handle_t key));
_CK_DECLARE_FUNCTION (C_Sign,
		      (ck_session_handle_t session,
		       unsigned char *data, unsigned long data_len,
		       unsigned char *signature,
		       unsigned long *signature_len));
_CK_DECLARE_FUNCTION (C_SignUpdate,
		      (ck_session_handle_t session,
		       unsigned char *part, unsigned long part_len));
_CK_DECLARE_FUNCTION (C_SignFinal,
		      (ck_session_handle_t session,
		       unsigned char *signature,
		       unsigned long *signature_len));
_CK_DECLARE_FUNCTION (C_SignRecoverInit,
		      (ck_session_handle_t session,
		       struct ck_mechanism *mechanism,
		       ck_object_handle_t key));
_CK_DECLARE_FUNCTION (C_SignRecover,
		      (ck_session_handle_t session,
		       unsigned char *data, unsigned long data_len,
		       unsigned char *signature,
		       unsigned long *signature_len));

_CK_DECLARE_FUNCTION (C_VerifyInit,
		      (ck_session_handle_t session,
		       struct ck_mechanism *mechanism,
		       ck_object_handle_t key));
_CK_DECLARE_FUNCTION (C_Verify,
		      (ck_session_handle_t session,
		       unsigned char *data, unsigned long data_len,
		       unsigned char *signature,
		       unsigned long signature_len));
_CK_DECLARE_FUNCTION (C_VerifyUpdate,
		      (ck_session_handle_t session,
		       unsigned char *part, unsigned long part_len));
_CK_DECLARE_FUNCTION (C_VerifyFinal,
		      (ck_session_handle_t session,
		       unsigned char *signature,
		       unsigned long signature_len));
_CK_DECLARE_FUNCTION (C_VerifyRecoverInit,
		      (ck_session_handle_t session,
		       struct ck_mechanism *mechanism,
		       ck_object_handle_t key));
_CK_DECLARE_FUNCTION (C_VerifyRecover,
		      (ck_session_handle_t session,
		       unsigned char *signature,
		       unsigned long signature_len,
		       unsigned char *data,
		       unsigned long *data_len));

_CK_DECLARE_FUNCTION (C_DigestEncryptUpdate,
		      (ck_session_handle_t session,
		       unsigned char *part, unsigned long part_len,
		       unsigned char *encrypted_part,
		       unsigned long *encrypted_part_len));
_CK_DECLARE_FUNCTION (C_DecryptDigestUpdate,
		      (ck_session_handle_t session,
		       unsigned char *encrypted_part,
		       unsigned long encrypted_part_len,
		       unsigned char *part,
		       unsigned long *part_len));
_CK_DECLARE_FUNCTION (C_SignEncryptUpdate,
		      (ck_session_handle_t session,
		       unsigned char *part, unsigned long part_len,
		       unsigned char *encrypted_part,
		       unsigned long *encrypted_part_len));
_CK_DECLARE_FUNCTION (C_DecryptVerifyUpdate,
		      (ck_session_handle_t session,
		       unsigned char *encrypted_part,
		       unsigned long encrypted_part_len,
		       unsigned char *part,
		       unsigned long *part_len));

_CK_DECLARE_FUNCTION (C_GenerateKey,
		      (ck_session_handle_t session,
		       struct ck_mechanism *mechanism,
		       struct ck_attribute *templ,
		       unsigned long count,
		       ck_object_handle_t *key));
_CK_DECLARE_FUNCTION (C_GenerateKeyPair,
		      (ck_session_handle_t session,
		       struct ck_mechanism *mechanism,
		       struct ck_attribute *public_key_template,
		       unsigned long public_key_attribute_count,
		       struct ck_attribute *private_key_template,
		       unsigned long private_key_attribute_count,
		       ck_object_handle_t *public_key,
		       ck_object_handle_t *private_key));
_CK_DECLARE_FUNCTION (C_WrapKey,
		      (ck_session_handle_t session,
		       struct ck_mechanism *mechanism,
		       ck_object_handle_t wrapping_key,
		       ck_object_handle_t key,
		       unsigned char *wrapped_key,
		       unsigned long *wrapped_key_len));
_CK_DECLARE_FUNCTION (C_UnwrapKey,
		      (ck_session_handle_t session,
		       struct ck_mechanism *mechanism,
		       ck_object_handle_t unwrapping_key,
		       unsigned char *wrapped_key,
		       unsigned long wrapped_key_len,
		       struct ck_attribute *templ,
		       unsigned long attribute_count,
		       ck_object_handle_t *key));
_CK_DECLARE_FUNCTION (C_DeriveKey,
		      (ck_session_handle_t session,
		       struct ck_mechanism *mechanism,
		       ck_object_handle_t base_key,
		       struct ck_attribute *templ,
		       unsigned long attribute_count,
		       ck_object_handle_t *key));

_CK_DECLARE_FUNCTION (C_SeedRandom,
		      (ck_session_handle_t session, unsigned char *seed,
		       unsigned long seed_len));
_CK_DECLARE_FUNCTION (C_GenerateRandom,
		      (ck_session_handle_t session,
		       unsigned char *random_data,
		       unsigned long random_len));

_CK_DECLARE_FUNCTION (C_GetFunctionStatus, (ck_session_handle_t session));
_CK_DECLARE_FUNCTION (C_CancelFunction, (ck_session_handle_t session));


struct ck_function_list
{
  struct ck_version version;
  CK_C_Initialize C_Initialize;
  CK_C_Finalize C_Finalize;
  CK_C_GetInfo C_GetInfo;
  CK_C_GetFunctionList C_GetFunctionList;
  CK_C_GetSlotList C_GetSlotList;
  CK_C_GetSlotInfo C_GetSlotInfo;
  CK_C_GetTokenInfo C_GetTokenInfo;
  CK_C_GetMechanismList C_GetMechanismList;
  CK_C_GetMechanismInfo C_GetMechanismInfo;
  CK_C_InitToken C_InitToken;
  CK_C_InitPIN C_InitPIN;
  CK_C_SetPIN C_SetPIN;
  CK_C_OpenSession C_OpenSession;
  CK_C_CloseSession C_CloseSession;
  CK_C_CloseAllSessions C_CloseAllSessions;
  CK_C_GetSessionInfo C_GetSessionInfo;
  CK_C_GetOperationState C_GetOperationState;
  CK_C_SetOperationState C_SetOperationState;
  CK_C_Login C_Login;
  CK_C_Logout C_Logout;
  CK_C_CreateObject C_CreateObject;
  CK_C_CopyObject C_CopyObject;
  CK_C_DestroyObject C_DestroyObject;
  CK_C_GetObjectSize C_GetObjectSize;
  CK_C_GetAttributeValue C_GetAttributeValue;
  CK_C_SetAttributeValue C_SetAttributeValue;
  CK_C_FindObjectsInit C_FindObjectsInit;
  CK_C_FindObjects C_FindObjects;
  CK_C_FindObjectsFinal C_FindObjectsFinal;
  CK_C_EncryptInit C_EncryptInit;
  CK_C_Encrypt C_Encrypt;
  CK_C_EncryptUpdate C_EncryptUpdate;
  CK_C_EncryptFinal C_EncryptFinal;
  CK_C_DecryptInit C_DecryptInit;
  CK_C_Decrypt C_Decrypt;
  CK_C_DecryptUpdate C_DecryptUpdate;
  CK_C_DecryptFinal C_DecryptFinal;
  CK_C_DigestInit C_DigestInit;
  CK_C_Digest C_Digest;
  CK_C_DigestUpdate C_DigestUpdate;
  CK_C_DigestKey C_DigestKey;
  CK_C_DigestFinal C_DigestFinal;
  CK_C_SignInit C_SignInit;
  CK_C_Sign C_Sign;
  CK_C_SignUpdate C_SignUpdate;
  CK_C_SignFinal C_SignFinal;
  CK_C_SignRecoverInit C_SignRecoverInit;
  CK_C_SignRecover C_SignRecover;
  CK_C_VerifyInit C_VerifyInit;
  CK_C_Verify C_Verify;
  CK_C_VerifyUpdate C_VerifyUpdate;
  CK_C_VerifyFinal C_VerifyFinal;
  CK_C_VerifyRecoverInit C_VerifyRecoverInit;
  CK_C_VerifyRecover C_VerifyRecover;
  CK_C_DigestEncryptUpdate C_DigestEncryptUpdate;
  CK_C_DecryptDigestUpdate C_DecryptDigestUpdate;
  CK_C_SignEncryptUpdate C_SignEncryptUpdate;
  CK_C_DecryptVerifyUpdate C_DecryptVerifyUpdate;
  CK_C_GenerateKey C_GenerateKey;
  CK_C_GenerateKeyPair C_GenerateKeyPair;
  CK_C_WrapKey C_WrapKey;
  CK_C_UnwrapKey C_UnwrapKey;
  CK_C_DeriveKey C_DeriveKey;
  CK_C_SeedRandom C_SeedRandom;
  CK_C_GenerateRandom C_GenerateRandom;
  CK_C_GetFunctionStatus C_GetFunctionStatus;
  CK_C_CancelFunction C_CancelFunction;
  CK_C_WaitForSlotEvent C_WaitForSlotEvent;
};


typedef ck_rv_t (*ck_createmutex_t) (void **mutex);
typedef ck_rv_t (*ck_destroymutex_t) (void *mutex);
typedef ck_rv_t (*ck_lockmutex_t) (void *mutex);
typedef ck_rv_t (*ck_unlockmutex_t) (void *mutex);


struct ck_c_initialize_args
{
  ck_createmutex_t create_mutex;
  ck_destroymutex_t destroy_mutex;
  ck_lockmutex_t lock_mutex;
  ck_unlockmutex_t unlock_mutex;
  ck_flags_t flags;
  void *reserved;
};


#define CKF_LIBRARY_CANT_CREATE_OS_THREADS	(1 << 0)
#define CKF_OS_LOCKING_OK			(1 << 1)

#define CKR_OK					(0)
#define CKR_CANCEL				(1)
#define CKR_HOST_MEMORY				(2)
#define CKR_SLOT_ID_INVALID			(3)
#define CKR_GENERAL_ERROR			(5)
#define CKR_FUNCTION_FAILED			(6)
#define CKR_ARGUMENTS_BAD			(7)
#define CKR_NO_EVENT				(8)
#define CKR_NEED_TO_CREATE_THREADS		(9)
#define CKR_CANT_LOCK				(0xa)
#define CKR_ATTRIBUTE_READ_ONLY			(0x10)
#define CKR_ATTRIBUTE_SENSITIVE			(0x11)
#define CKR_ATTRIBUTE_TYPE_INVALID		(0x12)
#define CKR_ATTRIBUTE_VALUE_INVALID		(0x13)
#define CKR_DATA_INVALID			(0x20)
#define CKR_DATA_LEN_RANGE			(0x21)
#define CKR_DEVICE_ERROR			(0x30)
#define CKR_DEVICE_MEMORY			(0x31)
#define CKR_DEVICE_REMOVED			(0x32)
#define CKR_ENCRYPTED_DATA_INVALID		(0x40)
#define CKR_ENCRYPTED_DATA_LEN_RANGE		(0x41)
#define CKR_FUNCTION_CANCELED			(0x50)
#define CKR_FUNCTION_NOT_PARALLEL		(0x51)
#define CKR_FUNCTION_NOT_SUPPORTED		(0x54)
#define CKR_KEY_HANDLE_INVALID			(0x60)
#define CKR_KEY_SIZE_RANGE			(0x62)
#define CKR_KEY_TYPE_INCONSISTENT		(0x63)
#define CKR_KEY_NOT_NEEDED			(0x64)
#define CKR_KEY_CHANGED				(0x65)
#define CKR_KEY_NEEDED				(0x66)
#define CKR_KEY_INDIGESTIBLE			(0x67)
#define CKR_KEY_FUNCTION_NOT_PERMITTED		(0x68)
#define CKR_KEY_NOT_WRAPPABLE			(0x69)
#define CKR_KEY_UNEXTRACTABLE			(0x6a)
#define CKR_MECHANISM_INVALID			(0x70)
#define CKR_MECHANISM_PARAM_INVALID		(0x71)
#define CKR_OBJECT_HANDLE_INVALID		(0x82)
#define CKR_OPERATION_ACTIVE			(0x90)
#define CKR_OPERATION_NOT_INITIALIZED		(0x91)
#define CKR_PIN_INCORRECT			(0xa0)
#define CKR_PIN_INVALID				(0xa1)
#define CKR_PIN_LEN_RANGE			(0xa2)
#define CKR_PIN_EXPIRED				(0xa3)
#define CKR_PIN_LOCKED				(0xa4)
#define CKR_SESSION_CLOSED			(0xb0)
#define CKR_SESSION_COUNT			(0xb1)
#define CKR_SESSION_HANDLE_INVALID		(0xb3)
#define CKR_SESSION_PARALLEL_NOT_SUPPORTED	(0xb4)
#define CKR_SESSION_READ_ONLY			(0xb5)
#define CKR_SESSION_EXISTS			(0xb6)
#define CKR_SESSION_READ_ONLY_EXISTS		(0xb7)
#define CKR_SESSION_READ_WRITE_SO_EXISTS	(0xb8)
#define CKR_SIGNATURE_INVALID			(0xc0)
#define CKR_SIGNATURE_LEN_RANGE			(0xc1)
#define CKR_TEMPLATE_INCOMPLETE			(0xd0)
#define CKR_TEMPLATE_INCONSISTENT		(0xd1)
#define CKR_TOKEN_NOT_PRESENT			(0xe0)
#define CKR_TOKEN_NOT_RECOGNIZED		(0xe1)
#define CKR_TOKEN_WRITE_PROTECTED		(0xe2)
#define	CKR_UNWRAPPING_KEY_HANDLE_INVALID	(0xf0)
#define CKR_UNWRAPPING_KEY_SIZE_RANGE		(0xf1)
#define CKR_UNWRAPPING_KEY_TYPE_INCONSISTENT	(0xf2)
#define CKR_USER_ALREADY_LOGGED_IN		(0x100)
#define CKR_USER_NOT_LOGGED_IN			(0x101)
#define CKR_USER_PIN_NOT_INITIALIZED		(0x102)
#define CKR_USER_TYPE_INVALID			(0x103)
#define CKR_USER_ANOTHER_ALREADY_LOGGED_IN	(0x104)
#define CKR_USER_TOO_MANY_TYPES			(0x105)
#define CKR_WRAPPED_KEY_INVALID			(0x110)
#define CKR_WRAPPED_KEY_LEN_RANGE		(0x112)
#define CKR_WRAPPING_KEY_HANDLE_INVALID		(0x113)
#define CKR_WRAPPING_KEY_SIZE_RANGE		(0x114)
#define CKR_WRAPPING_KEY_TYPE_INCONSISTENT	(0x115)
#define CKR_RANDOM_SEED_NOT_SUPPORTED		(0x120)
#define CKR_RANDOM_NO_RNG			(0x121)
#define CKR_DOMAIN_PARAMS_INVALID		(0x130)
#define CKR_BUFFER_TOO_SMALL			(0x150)
#define CKR_SAVED_STATE_INVALID			(0x160)
#define CKR_INFORMATION_SENSITIVE		(0x170)
#define CKR_STATE_UNSAVEABLE			(0x180)
#define CKR_CRYPTOKI_NOT_INITIALIZED		(0x190)
#define CKR_CRYPTOKI_ALREADY_INITIALIZED	(0x191)
#define CKR_MUTEX_BAD				(0x1a0)
#define CKR_MUTEX_NOT_LOCKED			(0x1a1)
#define CKR_FUNCTION_REJECTED			(0x200)
#define CKR_VENDOR_DEFINED			(1U << 31)



/* Compatibility layer.  */

#ifdef CRYPTOKI_COMPAT

#undef CK_DEFINE_FUNCTION
#define CK_DEFINE_FUNCTION(retval, name) retval CK_SPEC name

/* For NULL.  */
#include <stddef.h>

typedef unsigned char CK_BYTE;
typedef unsigned char CK_CHAR;
typedef unsigned char CK_UTF8CHAR;
typedef unsigned char CK_BBOOL;
typedef unsigned long int CK_ULONG;
typedef long int CK_LONG;
typedef CK_BYTE *CK_BYTE_PTR;
typedef CK_CHAR *CK_CHAR_PTR;
typedef CK_UTF8CHAR *CK_UTF8CHAR_PTR;
typedef CK_ULONG *CK_ULONG_PTR;
typedef void *CK_VOID_PTR;
typedef void **CK_VOID_PTR_PTR;
#define CK_FALSE 0
#define CK_TRUE 1
#ifndef CK_DISABLE_TRUE_FALSE
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
#endif

typedef struct ck_version CK_VERSION;
typedef struct ck_version *CK_VERSION_PTR;

typedef struct ck_info CK_INFO;
typedef struct ck_info *CK_INFO_PTR;

typedef ck_slot_id_t *CK_SLOT_ID_PTR;

typedef struct ck_slot_info CK_SLOT_INFO;
typedef struct ck_slot_info *CK_SLOT_INFO_PTR;

typedef struct ck_token_info CK_TOKEN_INFO;
typedef struct ck_token_info *CK_TOKEN_INFO_PTR;

typedef ck_session_handle_t *CK_SESSION_HANDLE_PTR;

typedef struct ck_session_info CK_SESSION_INFO;
typedef struct ck_session_info *CK_SESSION_INFO_PTR;

typedef ck_object_handle_t *CK_OBJECT_HANDLE_PTR;

typedef ck_object_class_t *CK_OBJECT_CLASS_PTR;

typedef struct ck_attribute CK_ATTRIBUTE;
typedef struct ck_attribute *CK_ATTRIBUTE_PTR;

typedef struct ck_date CK_DATE;
typedef struct ck_date *CK_DATE_PTR;

typedef ck_mechanism_type_t *CK_MECHANISM_TYPE_PTR;

typedef struct ck_mechanism CK_MECHANISM;
typedef struct ck_mechanism *CK_MECHANISM_PTR;

typedef struct ck_mechanism_info CK_MECHANISM_INFO;
typedef struct ck_mechanism_info *CK_MECHANISM_INFO_PTR;

typedef struct ck_function_list CK_FUNCTION_LIST;
typedef struct ck_function_list *CK_FUNCTION_LIST_PTR;
typedef struct ck_function_list **CK_FUNCTION_LIST_PTR_PTR;

typedef struct ck_c_initialize_args CK_C_INITIALIZE_ARGS;
typedef struct ck_c_initialize_args *CK_C_INITIALIZE_ARGS_PTR;

#define NULL_PTR NULL

/* Delete the helper macros defined at the top of the file.  */
#undef ck_flags_t
#undef ck_version

#undef ck_info
#undef cryptoki_version
#undef manufacturer_id
#undef library_description
#undef library_version

#undef ck_notification_t
#undef ck_slot_id_t

#undef ck_slot_info
#undef slot_description
#undef hardware_version
#undef firmware_version

#undef ck_token_info
#undef serial_number
#undef max_session_count
#undef session_count
#undef max_rw_session_count
#undef rw_session_count
#undef max_pin_len
#undef min_pin_len
#undef total_public_memory
#undef free_public_memory
#undef total_private_memory
#undef free_private_memory
#undef utc_time

#undef ck_session_handle_t
#undef ck_user_type_t
#undef ck_state_t

#undef ck_session_info
#undef slot_id
#undef device_error

#undef ck_object_handle_t
#undef ck_object_class_t
#undef ck_hw_feature_type_t
#undef ck_key_type_t
#undef ck_certificate_type_t
#undef ck_attribute_type_t

#undef ck_attribute
#undef value
#undef value_len

#undef ck_date

#undef ck_mechanism_type_t

#undef ck_mechanism
#undef parameter
#undef parameter_len

#undef ck_mechanism_info
#undef min_key_size
#undef max_key_size

#undef ck_rv_t
#undef ck_notify_t

#undef ck_function_list

#undef ck_createmutex_t
#undef ck_destroymutex_t
#undef ck_lockmutex_t
#undef ck_unlockmutex_t

#undef ck_c_initialize_args
#undef create_mutex
#undef destroy_mutex
#undef lock_mutex
#undef unlock_mutex
#undef reserved

#endif	/* CRYPTOKI_COMPAT */


/* System dependencies.  */
#if defined(_WIN32) || defined(CRYPTOKI_FORCE_WIN32)
#pragma pack(pop, cryptoki)
#endif

#if defined(__cplusplus)
}
#endif

#endif	/* PKCS11_H */
