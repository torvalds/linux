" Vim syntax file
" Language:     C libdns
" Maintainer:   miekg
" Last change:  2011-09-15

" ldns/util.h
syn keyword  ldnsMacro LDNS_MALLOC
syn keyword  ldnsMacro LDNS_XMALLOC
syn keyword  ldnsMacro LDNS_CALLOC
syn keyword  ldnsMacro LDNS_REALLOC
syn keyword  ldnsMacro LDNS_XREALLOC
syn keyword  ldnsMacro LDNS_FREE
syn keyword  ldnsMacro LDNS_DEP  
syn keyword  ldnsMacro LDNS_VERSION
syn keyword  ldnsMacro LDNS_REVISION

" ldns/dname.h
syn keyword  ldnsMacro          LDNS_DNAME_NORMALIZE

" ldns/tsig.h
syn keyword  ldnsType           ldns_tsig_credentials

" ldns/update.h
" --

" ldns/rdata.h
syn keyword  ldnsType           ldns_rdf
syn keyword  ldnsType           ldns_rdf_type
syn keyword  ldnsType           ldns_cert_algorithm
syn keyword  ldnsConstant	LDNS_RDF_TYPE_NONE
syn keyword  ldnsConstant	LDNS_RDF_TYPE_DNAME
syn keyword  ldnsConstant	LDNS_RDF_TYPE_INT8
syn keyword  ldnsConstant	LDNS_RDF_TYPE_INT16
syn keyword  ldnsConstant	LDNS_RDF_TYPE_INT16_DATA
syn keyword  ldnsConstant	LDNS_RDF_TYPE_INT32
syn keyword  ldnsConstant	LDNS_RDF_TYPE_A
syn keyword  ldnsConstant	LDNS_RDF_TYPE_AAAA
syn keyword  ldnsConstant	LDNS_RDF_TYPE_STR
syn keyword  ldnsConstant	LDNS_RDF_TYPE_APL
syn keyword  ldnsConstant	LDNS_RDF_TYPE_B32_EXT
syn keyword  ldnsConstant	LDNS_RDF_TYPE_B64
syn keyword  ldnsConstant	LDNS_RDF_TYPE_HEX
syn keyword  ldnsConstant	LDNS_RDF_TYPE_NSEC
syn keyword  ldnsConstant	LDNS_RDF_TYPE_TYPE
syn keyword  ldnsConstant	LDNS_RDF_TYPE_CLASS
syn keyword  ldnsConstant	LDNS_RDF_TYPE_CERT
syn keyword  ldnsConstant	LDNS_RDF_TYPE_CERT_ALG
syn keyword  ldnsConstant	LDNS_RDF_TYPE_ALG
syn keyword  ldnsConstant 	LDNS_RDF_TYPE_UNKNOWN
syn keyword  ldnsConstant	LDNS_RDF_TYPE_TIME
syn keyword  ldnsConstant	LDNS_RDF_TYPE_PERIOD
syn keyword  ldnsConstant	LDNS_RDF_TYPE_TSIGTIME
syn keyword  ldnsConstant	LDNS_RDF_TYPE_SERVICE
syn keyword  ldnsConstant	LDNS_RDF_TYPE_LOC
syn keyword  ldnsConstant	LDNS_RDF_TYPE_WKS
syn keyword  ldnsConstant	LDNS_RDF_TYPE_NSAP
syn keyword  ldnsConstant	LDNS_RDF_TYPE_ATMA
syn keyword  ldnsConstant	LDNS_RDF_TYPE_NSEC3_SALT
syn keyword  ldnsConstant	LDNS_RDF_TYPE_NSEC3_NEXT_OWNER
syn keyword  ldnsConstant	LDNS_RDF_TYPE_IPSECKEY
syn keyword  ldnsConstant	LDNS_RDF_TYPE_TSIG
syn keyword  ldnsConstant	LDNS_MAX_RDFLEN
syn keyword  ldnsConstant       LDNS_RDF_SIZE_BYTE             
syn keyword  ldnsConstant       LDNS_RDF_SIZE_WORD             
syn keyword  ldnsConstant       LDNS_RDF_SIZE_DOUBLEWORD       
syn keyword  ldnsConstant       LDNS_RDF_SIZE_6BYTES           
syn keyword  ldnsConstant       LDNS_RDF_SIZE_16BYTES          
syn keyword  ldnsConstant       LDNS_NSEC3_VARS_OPTOUT_MASK

syn keyword  ldnsConstant       LDNS_CERT_PKIX
syn keyword  ldnsConstant       LDNS_CERT_SPKI
syn keyword  ldnsConstant       LDNS_CERT_PGP
syn keyword  ldnsConstant       LDNS_CERT_IPKIX
syn keyword  ldnsConstant       LDNS_CERT_ISPKI
syn keyword  ldnsConstant       LDNS_CERT_IPGP
syn keyword  ldnsConstant       LDNS_CERT_ACPKIX
syn keyword  ldnsConstant       LDNS_CERT_IACPKIX
syn keyword  ldnsConstant       LDNS_CERT_URI
syn keyword  ldnsConstant       LDNS_CERT_OID

" ldns/ldns.h
syn keyword  ldnsConstant	LDNS_PORT
syn keyword  ldnsConstant	LDNS_IP4ADDRLEN
syn keyword  ldnsConstant	LDNS_IP6ADDRLEN
syn keyword  ldnsConstant	LDNS_ROOT_LABEL_STR
syn keyword  ldnsConstant	LDNS_DEFAULT_TTL

" ldns/packet.h
syn keyword  ldnsType           ldns_pkt
syn keyword  ldnsType           ldns_pkt_section
syn keyword  ldnsType		ldns_pkt_type
syn keyword  ldnsType		ldns_pkt_opcode
syn keyword  ldnsType		ldns_pkt_rcode
syn keyword  ldnsType		ldns_hdr
syn keyword  ldnsConstant	LDNS_QR
syn keyword  ldnsConstant	LDNS_AA
syn keyword  ldnsConstant	LDNS_TC
syn keyword  ldnsConstant	LDNS_RD
syn keyword  ldnsConstant	LDNS_CD
syn keyword  ldnsConstant	LDNS_RA
syn keyword  ldnsConstant	LDNS_AD
syn keyword  ldnsConstant	LDNS_MAX_PACKETLEN
syn keyword  ldnsConstant	LDNS_PACKET_QUESTION
syn keyword  ldnsConstant	LDNS_PACKET_REFERRAL
syn keyword  ldnsConstant	LDNS_PACKET_ANSWER
syn keyword  ldnsConstant	LDNS_PACKET_NXDOMAIN
syn keyword  ldnsConstant	LDNS_PACKET_NODATA
syn keyword  ldnsConstant	LDNS_PACKET_UNKNOWN
syn keyword  ldnsConstant	LDNS_SECTION_QUESTION
syn keyword  ldnsConstant	LDNS_SECTION_ANSWER
syn keyword  ldnsConstant	LDNS_SECTION_AUTHORITY
syn keyword  ldnsConstant	LDNS_SECTION_ADDITIONAL
syn keyword  ldnsConstant	LDNS_SECTION_ANY
syn keyword  ldnsConstant	LDNS_SECTION_ANY_NOQUESTION
syn keyword  ldnsConstant	LDNS_PACKET_QUERY
syn keyword  ldnsConstant	LDNS_PACKET_IQUERY
syn keyword  ldnsConstant	LDNS_PACKET_STATUS
syn keyword  ldnsConstant	LDNS_PACKET_NOTIFY
syn keyword  ldnsConstant	LDNS_PACKET_UPDATE

syn keyword  ldnsConstant       LDNS_RCODE_NOERROR
syn keyword  ldnsConstant       LDNS_RCODE_FORMERR
syn keyword  ldnsConstant       LDNS_RCODE_SERVFAIL
syn keyword  ldnsConstant       LDNS_RCODE_NXDOMAIN
syn keyword  ldnsConstant       LDNS_RCODE_NOTIMPL
syn keyword  ldnsConstant       LDNS_RCODE_REFUSED
syn keyword  ldnsConstant       LDNS_RCODE_YXDOMAIN 
syn keyword  ldnsConstant       LDNS_RCODE_YXRRSET
syn keyword  ldnsConstant       LDNS_RCODE_NXRRSET
syn keyword  ldnsConstant       LDNS_RCODE_NOTAUTH
syn keyword  ldnsConstant       LDNS_RCODE_NOTZONE

" dns/error.h
syn keyword ldnsConstant	LDNS_STATUS_OK
syn keyword ldnsConstant	LDNS_STATUS_EMPTY_LABEL
syn keyword ldnsConstant	LDNS_STATUS_LABEL_OVERFLOW
syn keyword ldnsConstant	LDNS_STATUS_DOMAINNAME_OVERFLOW
syn keyword ldnsConstant	LDNS_STATUS_DOMAINNAME_UNDERFLOW
syn keyword ldnsConstant	LDNS_STATUS_DDD_OVERFLOW
syn keyword ldnsConstant	LDNS_STATUS_PACKET_OVERFLOW
syn keyword ldnsConstant	LDNS_STATUS_INVALID_POINTER
syn keyword ldnsConstant	LDNS_STATUS_MEM_ERR
syn keyword ldnsConstant	LDNS_STATUS_INTERNAL_ERR
syn keyword ldnsConstant	LDNS_STATUS_SSL_ERR
syn keyword ldnsConstant	LDNS_STATUS_ERR
syn keyword ldnsConstant	LDNS_STATUS_INVALID_INT
syn keyword ldnsConstant	LDNS_STATUS_INVALID_IP4
syn keyword ldnsConstant	LDNS_STATUS_INVALID_IP6
syn keyword ldnsConstant	LDNS_STATUS_INVALID_STR
syn keyword ldnsConstant	LDNS_STATUS_INVALID_B32_EXT
syn keyword ldnsConstant	LDNS_STATUS_INVALID_B64
syn keyword ldnsConstant	LDNS_STATUS_INVALID_HEX
syn keyword ldnsConstant	LDNS_STATUS_INVALID_TIME
syn keyword ldnsConstant	LDNS_STATUS_NETWORK_ERR
syn keyword ldnsConstant	LDNS_STATUS_ADDRESS_ERR
syn keyword ldnsConstant	LDNS_STATUS_FILE_ERR
syn keyword ldnsConstant	LDNS_STATUS_UNKNOWN_INET
syn keyword ldnsConstant	LDNS_STATUS_NOT_IMPL
syn keyword ldnsConstant	LDNS_STATUS_NULL
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_UNKNOWN_ALGO
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_ALGO_NOT_IMPL
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_NO_RRSIG
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_NO_DNSKEY
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_NO_TRUSTED_DNSKEY
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_NO_DS
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_NO_TRUSTED_DS
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_NO_MATCHING_KEYTAG_DNSKEY
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_VALIDATED
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_BOGUS
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_SIG_EXPIRED
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_SIG_NOT_INCEPTED
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_TSIG_BOGUS
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_TSIG_ERR
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_EXPIRATION_BEFORE_INCEPTION
syn keyword ldnsConstant	LDNS_STATUS_CRYPTO_TYPE_COVERED_ERR
syn keyword ldnsConstant	LDNS_STATUS_ENGINE_KEY_NOT_LOADED
syn keyword ldnsConstant	LDNS_STATUS_NSEC3_ERR
syn keyword ldnsConstant	LDNS_STATUS_RES_NO_NS
syn keyword ldnsConstant	LDNS_STATUS_RES_QUERY
syn keyword ldnsConstant	LDNS_STATUS_WIRE_INCOMPLETE_HEADER
syn keyword ldnsConstant	LDNS_STATUS_WIRE_INCOMPLETE_QUESTION
syn keyword ldnsConstant	LDNS_STATUS_WIRE_INCOMPLETE_ANSWER
syn keyword ldnsConstant	LDNS_STATUS_WIRE_INCOMPLETE_AUTHORITY
syn keyword ldnsConstant	LDNS_STATUS_WIRE_INCOMPLETE_ADDITIONAL
syn keyword ldnsConstant	LDNS_STATUS_NO_DATA
syn keyword ldnsConstant	LDNS_STATUS_CERT_BAD_ALGORITHM
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_TYPE_ERR
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_CLASS_ERR
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_TTL_ERR
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_INCLUDE_ERR_NOTIMPL
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_RDATA_ERR
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_DNAME_ERR
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_VERSION_ERR
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_ALG_ERR
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_KEYWORD_ERR
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_TTL
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_ORIGIN
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_INCLUDE
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_EMPTY
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_ITERATIONS_OVERFLOW
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_MISSING_VALUE_ERR
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_INTEGER_OVERFLOW
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_BAD_ESCAPE
syn keyword ldnsConstant	LDNS_STATUS_SOCKET_ERROR
syn keyword ldnsConstant	LDNS_STATUS_SYNTAX_ERR
syn keyword ldnsConstant	LDNS_STATUS_DNSSEC_EXISTENCE_DENIED
syn keyword ldnsConstant	LDNS_STATUS_DNSSEC_NSEC_RR_NOT_COVERED
syn keyword ldnsConstant	LDNS_STATUS_DNSSEC_NSEC_WILDCARD_NOT_COVERED
syn keyword ldnsConstant	LDNS_STATUS_DNSSEC_NSEC3_ORIGINAL_NOT_FOUND
syn keyword ldnsConstant	LDNS_STATUS_MISSING_RDATA_FIELDS_RRSIG
syn keyword ldnsConstant	LDNS_STATUS_MISSING_RDATA_FIELDS_KEY


" ldns/resolver.h
syn keyword  ldnsType	  	ldns_resolver
syn keyword  ldnsConstant       LDNS_RESOLV_CONF
syn keyword  ldnsConstant       LDNS_RESOLV_HOSTS
syn keyword  ldnsConstant       LDNS_RESOLV_KEYWORD
syn keyword  ldnsConstant       LDNS_RESOLV_DEFDOMAIN
syn keyword  ldnsConstant       LDNS_RESOLV_NAMESERVER
syn keyword  ldnsConstant       LDNS_RESOLV_SEARCH
syn keyword  ldnsConstant       LDNS_RESOLV_SORTLIST
syn keyword  ldnsConstant       LDNS_RESOLV_OPTIONS
syn keyword  ldnsConstant       LDNS_RESOLV_ANCHOR
syn keyword  ldnsConstant       LDNS_RESOLV_KEYWORDS
syn keyword  ldnsConstant       LDNS_RESOLV_INETANY
syn keyword  ldnsConstant       LDNS_RESOLV_INET
syn keyword  ldnsConstant       LDNS_RESOLV_INET6
syn keyword  ldnsConstant       LDNS_RESOLV_RTT_INF
syn keyword  ldnsConstant       LDNS_RESOLV_RTT_MIN

" ldns/zone.h
syn keyword  ldnsType	  	ldns_zone

" ldns/dnssec.h
syn keyword  ldnsConstant       LDNS_NSEC3_MAX_ITERATIONS
syn keyword  ldnsConstant       LDNS_DEFAULT_EXP_TIME
syn keyword  ldnsConstant       LDNS_DNSSEC_KEYPROTO
syn keyword  ldnsConstant	LDNS_MAX_KEYLEN
" ldns/dnssec_sign.h
" --
" ldns/dnssec_zone.h
syn keyword  ldnsType           ldns_dnssec_rrs
syn keyword  ldnsType           ldns_dnssec_rrsets
syn keyword  ldnsType           ldns_dnssec_name
syn keyword  ldnsType           ldns_dnssec_zone
" ldns/dnssec_verify.h
syn keyword  ldnsType           ldns_dnssec_data_chain
syn keyword  ldnsType           ldns_dnssec_trust_tree

" ldns/rr.h 
syn keyword  ldnsType	  	ldns_rr_list 
syn keyword  ldnsType           ldns_rr_descriptor
syn keyword  ldnsType           ldns_rr
syn keyword  ldnsType           ldns_rr_type
syn keyword  ldnsType           ldns_rr_class
syn keyword  ldnsType		ldns_rr_compress

syn keyword  ldnsConstant       LDNS_MAX_LABELLEN     
syn keyword  ldnsConstant       LDNS_MAX_DOMAINLEN
syn keyword  ldnsConstant       LDNS_RR_COMPRESS
syn keyword  ldnsConstant       LDNS_RR_NO_COMPRESS

syn keyword  ldnsConstant	LDNS_RR_CLASS_IN
syn keyword  ldnsConstant	LDNS_RR_CLASS_CH
syn keyword  ldnsConstant	LDNS_RR_CLASS_HS  
syn keyword  ldnsConstant	LDNS_RR_CLASS_NONE
syn keyword  ldnsConstant	LDNS_RR_CLASS_ANY 

syn keyword ldnsConstant	LDNS_RR_TYPE_A
syn keyword ldnsConstant	LDNS_RR_TYPE_A6
syn keyword ldnsConstant	LDNS_RR_TYPE_AAAA
syn keyword ldnsConstant	LDNS_RR_TYPE_AFSDB
syn keyword ldnsConstant	LDNS_RR_TYPE_ANY
syn keyword ldnsConstant	LDNS_RR_TYPE_APL
syn keyword ldnsConstant	LDNS_RR_TYPE_ATMA
syn keyword ldnsConstant	LDNS_RR_TYPE_AXFR
syn keyword ldnsConstant	LDNS_RR_TYPE_CERT
syn keyword ldnsConstant	LDNS_RR_TYPE_CNAME
syn keyword ldnsConstant	LDNS_RR_TYPE_COUNT
syn keyword ldnsConstant	LDNS_RR_TYPE_DHCID
syn keyword ldnsConstant	LDNS_RR_TYPE_DLV
syn keyword ldnsConstant	LDNS_RR_TYPE_DNAME
syn keyword ldnsConstant	LDNS_RR_TYPE_DNSKEY
syn keyword ldnsConstant	LDNS_RR_TYPE_DS
syn keyword ldnsConstant	LDNS_RR_TYPE_EID
syn keyword ldnsConstant	LDNS_RR_TYPE_FIRST
syn keyword ldnsConstant	LDNS_RR_TYPE_GID
syn keyword ldnsConstant	LDNS_RR_TYPE_GPOS
syn keyword ldnsConstant	LDNS_RR_TYPE_HINFO
syn keyword ldnsConstant	LDNS_RR_TYPE_IPSECKEY
syn keyword ldnsConstant	LDNS_RR_TYPE_ISDN
syn keyword ldnsConstant	LDNS_RR_TYPE_IXFR
syn keyword ldnsConstant	LDNS_RR_TYPE_KEY
syn keyword ldnsConstant	LDNS_RR_TYPE_KX
syn keyword ldnsConstant	LDNS_RR_TYPE_LAST 
syn keyword ldnsConstant	LDNS_RR_TYPE_LOC
syn keyword ldnsConstant	LDNS_RR_TYPE_MAILA
syn keyword ldnsConstant	LDNS_RR_TYPE_MAILB
syn keyword ldnsConstant	LDNS_RR_TYPE_MB
syn keyword ldnsConstant	LDNS_RR_TYPE_MD
syn keyword ldnsConstant	LDNS_RR_TYPE_MF
syn keyword ldnsConstant	LDNS_RR_TYPE_MG
syn keyword ldnsConstant	LDNS_RR_TYPE_MINFO
syn keyword ldnsConstant	LDNS_RR_TYPE_MR
syn keyword ldnsConstant	LDNS_RR_TYPE_MX
syn keyword ldnsConstant	LDNS_RR_TYPE_NAPTR
syn keyword ldnsConstant	LDNS_RR_TYPE_NIMLOC
syn keyword ldnsConstant	LDNS_RR_TYPE_NS
syn keyword ldnsConstant	LDNS_RR_TYPE_NSAP
syn keyword ldnsConstant	LDNS_RR_TYPE_NSAP_PTR
syn keyword ldnsConstant	LDNS_RR_TYPE_NSEC
syn keyword ldnsConstant	LDNS_RR_TYPE_NSEC3
syn keyword ldnsConstant	LDNS_RR_TYPE_NSEC3
syn keyword ldnsConstant	LDNS_RR_TYPE_NSEC3PARAM
syn keyword ldnsConstant	LDNS_RR_TYPE_NSEC3PARAMS
syn keyword ldnsConstant	LDNS_RR_TYPE_NSEC3PARAMS
syn keyword ldnsConstant	LDNS_RR_TYPE_NULL
syn keyword ldnsConstant	LDNS_RR_TYPE_NXT
syn keyword ldnsConstant	LDNS_RR_TYPE_OPT
syn keyword ldnsConstant	LDNS_RR_TYPE_PTR
syn keyword ldnsConstant	LDNS_RR_TYPE_PX
syn keyword ldnsConstant	LDNS_RR_TYPE_RP
syn keyword ldnsConstant	LDNS_RR_TYPE_RRSIG
syn keyword ldnsConstant	LDNS_RR_TYPE_RT
syn keyword ldnsConstant	LDNS_RR_TYPE_SIG
syn keyword ldnsConstant	LDNS_RR_TYPE_SINK
syn keyword ldnsConstant	LDNS_RR_TYPE_SOA
syn keyword ldnsConstant	LDNS_RR_TYPE_SPF
syn keyword ldnsConstant	LDNS_RR_TYPE_SRV
syn keyword ldnsConstant	LDNS_RR_TYPE_SSHFP
syn keyword ldnsConstant        LDNS_RR_TYPE_TALINK
syn keyword ldnsConstant	LDNS_RR_TYPE_TSIG
syn keyword ldnsConstant	LDNS_RR_TYPE_TXT
syn keyword ldnsConstant	LDNS_RR_TYPE_UID
syn keyword ldnsConstant	LDNS_RR_TYPE_UINFO
syn keyword ldnsConstant	LDNS_RR_TYPE_UNSPEC
syn keyword ldnsConstant	LDNS_RR_TYPE_WKS
syn keyword ldnsConstant	LDNS_RR_TYPE_X25

" ldns/buffer.h
syn keyword  ldnsType		ldns_buffer
syn keyword  ldnsConstant	LDNS_MIN_BUFLEN

" ldns/host2str.h
syn keyword  ldnsConstant       LDNS_APL_IP4
syn keyword  ldnsConstant       LDNS_APL_IP6
syn keyword  ldnsConstant       LDNS_APL_MASK
syn keyword  ldnsConstant       LDNS_APL_NEGATION

" ldns/keys.h
syn keyword  ldnsType           ldns_key
syn keyword  ldnsType           ldns_key_list
syn keyword  ldnsType           ldns_signing_algorithm
syn keyword  ldnsType           ldns_hash
syn keyword  ldnsType           ldns_algorithm
syn keyword  ldnsConstant       LDNS_KEY_ZONE_KEY
syn keyword  ldnsConstant       LDNS_KEY_SEP_KEY
syn keyword  ldnsConstant       LDNS_KEY_REVOKE_KEY
syn keyword  ldnsConstant       LDNS_SHA1
syn keyword  ldnsConstant       LDNS_SHA256
syn keyword  ldnsConstant       LDNS_HASH_GOST
syn keyword  ldnsConstant       LDNS_SHA384

syn keyword  ldnsConstant       LDNS_SIGN_RSAMD5
syn keyword  ldnsConstant       LDNS_SIGN_RSASHA1
syn keyword  ldnsConstant       LDNS_SIGN_DSA
syn keyword  ldnsConstant       LDNS_SIGN_RSASHA1_NSEC3
syn keyword  ldnsConstant       LDNS_SIGN_RSASHA256
syn keyword  ldnsConstant       LDNS_SIGN_RSASHA512
syn keyword  ldnsConstant       LDNS_SIGN_DSA_NSEC3
syn keyword  ldnsConstant       LDNS_SIGN_ECC_GOST
syn keyword  ldnsConstant       LDNS_SIGN_ECDSAP256SHA256
syn keyword  ldnsConstant       LDNS_SIGN_ECDSAP384SHA384
syn keyword  ldnsConstant       LDNS_SIGN_HMACMD5
syn keyword  ldnsConstant       LDNS_SIGN_HMACSHA1
syn keyword  ldnsConstant       LDNS_SIGN_HMACSHA256

" ldns/wire2host.h
syn keyword  ldnsMacro          LDNS_WIRE2HOST_H
syn keyword  ldnsMacro          LDNS_HEADER_SIZE
syn keyword  ldnsMacro          LDNS_RD_MASK
syn keyword  ldnsMacro          LDNS_RD_SHIFT
syn keyword  ldnsMacro          LDNS_RD_WIRE
syn keyword  ldnsMacro          LDNS_RD_SET
syn keyword  ldnsMacro          LDNS_RD_CLR
syn keyword  ldnsMacro          LDNS_TC_MASK
syn keyword  ldnsMacro          LDNS_TC_SHIFT
syn keyword  ldnsMacro          LDNS_TC_WIRE
syn keyword  ldnsMacro          LDNS_TC_SET
syn keyword  ldnsMacro          LDNS_TC_CLR
syn keyword  ldnsMacro          LDNS_AA_MASK
syn keyword  ldnsMacro          LDNS_AA_SHIFT
syn keyword  ldnsMacro          LDNS_AA_WIRE
syn keyword  ldnsMacro          LDNS_AA_SET
syn keyword  ldnsMacro          LDNS_AA_CLR
syn keyword  ldnsMacro          LDNS_OPCODE_MASK
syn keyword  ldnsMacro          LDNS_OPCODE_SHIFT
syn keyword  ldnsMacro          LDNS_OPCODE_WIRE
syn keyword  ldnsMacro          LDNS_OPCODE_SET
syn keyword  ldnsMacro          LDNS_QR_MASK
syn keyword  ldnsMacro          LDNS_QR_SHIFT
syn keyword  ldnsMacro          LDNS_QR_WIRE
syn keyword  ldnsMacro          LDNS_QR_SET
syn keyword  ldnsMacro          LDNS_QR_CLR
syn keyword  ldnsMacro          LDNS_RCODE_MASK
syn keyword  ldnsMacro          LDNS_RCODE_SHIFT
syn keyword  ldnsMacro          LDNS_RCODE_WIRE
syn keyword  ldnsMacro          LDNS_RCODE_SET
syn keyword  ldnsMacro          LDNS_CD_MASK
syn keyword  ldnsMacro          LDNS_CD_SHIFT
syn keyword  ldnsMacro          LDNS_CD_WIRE
syn keyword  ldnsMacro          LDNS_CD_SET
syn keyword  ldnsMacro          LDNS_CD_CLR
syn keyword  ldnsMacro          LDNS_AD_MASK
syn keyword  ldnsMacro          LDNS_AD_SHIFT
syn keyword  ldnsMacro          LDNS_AD_WIRE
syn keyword  ldnsMacro          LDNS_AD_SET
syn keyword  ldnsMacro          LDNS_AD_CLR
syn keyword  ldnsMacro          LDNS_Z_MASK
syn keyword  ldnsMacro          LDNS_Z_SHIFT
syn keyword  ldnsMacro          LDNS_Z_WIRE
syn keyword  ldnsMacro          LDNS_Z_SET
syn keyword  ldnsMacro          LDNS_Z_CLR
syn keyword  ldnsMacro          LDNS_RA_MASK
syn keyword  ldnsMacro          LDNS_RA_SHIFT
syn keyword  ldnsMacro          LDNS_RA_WIRE
syn keyword  ldnsMacro          LDNS_RA_SET
syn keyword  ldnsMacro          LDNS_RA_CLR
syn keyword  ldnsMacro          LDNS_ID_WIRE
syn keyword  ldnsMacro          LDNS_ID_SET
syn keyword  ldnsMacro          LDNS_QDCOUNT_OFF
syn keyword  ldnsMacro          QDCOUNT
syn keyword  ldnsMacro          LDNS_QDCOUNT
syn keyword  ldnsMacro          LDNS_ANCOUNT_OFF
syn keyword  ldnsMacro          LDNS_ANCOUNT
syn keyword  ldnsMacro          LDNS_NSCOUNT_OFF
syn keyword  ldnsMacro          LDNS_NSCOUNT
syn keyword  ldnsMacro          LDNS_ARCOUNT_OFF
syn keyword  ldnsMacro          LDNS_ARCOUNT

" ldns/host2wire.h
" --

" ldns/* -- All functions
" Created with:
" Get all the functions that start with 'ldns_'
" egrep '^[a-z_]+ [*a-z_0-9]+\(' *.h | sed -e 's/(.*$//' | awk '{print $2}' | \
" sed 's/^\*//' | grep '^ldns' | sort
" Not included, but could be added...?

" Default highlighting
command -nargs=+ HiLink hi def link <args>
HiLink ldnsType                Type
" Currently no functions are defined
HiLink ldnsFunction            Function 
HiLink ldnsMacro               Macro
HiLink ldnsConstant            Constant
delcommand HiLink
