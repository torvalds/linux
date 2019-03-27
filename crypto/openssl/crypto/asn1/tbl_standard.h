/*
 * Copyright 1999-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* size limits: this stuff is taken straight from RFC3280 */

#define ub_name                         32768
#define ub_common_name                  64
#define ub_locality_name                128
#define ub_state_name                   128
#define ub_organization_name            64
#define ub_organization_unit_name       64
#define ub_title                        64
#define ub_email_address                128
#define ub_serial_number                64

/* From RFC4524 */

#define ub_rfc822_mailbox               256

/* This table must be kept in NID order */

static const ASN1_STRING_TABLE tbl_standard[] = {
    {NID_commonName, 1, ub_common_name, DIRSTRING_TYPE, 0},
    {NID_countryName, 2, 2, B_ASN1_PRINTABLESTRING, STABLE_NO_MASK},
    {NID_localityName, 1, ub_locality_name, DIRSTRING_TYPE, 0},
    {NID_stateOrProvinceName, 1, ub_state_name, DIRSTRING_TYPE, 0},
    {NID_organizationName, 1, ub_organization_name, DIRSTRING_TYPE, 0},
    {NID_organizationalUnitName, 1, ub_organization_unit_name, DIRSTRING_TYPE,
     0},
    {NID_pkcs9_emailAddress, 1, ub_email_address, B_ASN1_IA5STRING,
     STABLE_NO_MASK},
    {NID_pkcs9_unstructuredName, 1, -1, PKCS9STRING_TYPE, 0},
    {NID_pkcs9_challengePassword, 1, -1, PKCS9STRING_TYPE, 0},
    {NID_pkcs9_unstructuredAddress, 1, -1, DIRSTRING_TYPE, 0},
    {NID_givenName, 1, ub_name, DIRSTRING_TYPE, 0},
    {NID_surname, 1, ub_name, DIRSTRING_TYPE, 0},
    {NID_initials, 1, ub_name, DIRSTRING_TYPE, 0},
    {NID_serialNumber, 1, ub_serial_number, B_ASN1_PRINTABLESTRING,
     STABLE_NO_MASK},
    {NID_friendlyName, -1, -1, B_ASN1_BMPSTRING, STABLE_NO_MASK},
    {NID_name, 1, ub_name, DIRSTRING_TYPE, 0},
    {NID_dnQualifier, -1, -1, B_ASN1_PRINTABLESTRING, STABLE_NO_MASK},
    {NID_domainComponent, 1, -1, B_ASN1_IA5STRING, STABLE_NO_MASK},
    {NID_ms_csp_name, -1, -1, B_ASN1_BMPSTRING, STABLE_NO_MASK},
    {NID_rfc822Mailbox, 1, ub_rfc822_mailbox, B_ASN1_IA5STRING,
     STABLE_NO_MASK},
    {NID_jurisdictionCountryName, 2, 2, B_ASN1_PRINTABLESTRING, STABLE_NO_MASK},
    {NID_INN, 1, 12, B_ASN1_NUMERICSTRING, STABLE_NO_MASK},
    {NID_OGRN, 1, 13, B_ASN1_NUMERICSTRING, STABLE_NO_MASK},
    {NID_SNILS, 1, 11, B_ASN1_NUMERICSTRING, STABLE_NO_MASK},
    {NID_countryCode3c, 3, 3, B_ASN1_PRINTABLESTRING, STABLE_NO_MASK},
    {NID_countryCode3n, 3, 3, B_ASN1_NUMERICSTRING, STABLE_NO_MASK},
    {NID_dnsName, 0, -1, B_ASN1_UTF8STRING, STABLE_NO_MASK}
};

