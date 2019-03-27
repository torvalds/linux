.pl 10.0i
.po 0
.ll 8.5i
.lt 8.5i
.nr LL 8.5i
.nr LT 8.5i
.ds LF Johansson
.ds RF [Page %]
.ds CF
.ds LH Internet draft
.ds RH 30 March 1998
.ds CH Berkeley AMD LDAP Schema
.hy 0
.ad l
.in 0
INTERNET-DRAFT                                                         Leif Johansson
Intented Category: Experimental                                  Stockholm University



.ce
A directory (X.500 and LDAPv3) schema for Berkely automounter


.ti 0
1. Status of this Memo

.fi
.in 3
This memo describes a directory (LDAP or X.500) schema for storing
amd (Berkely-style automounter) mount info maps. The schema is currently
beeing supported by the (beta version of the) am-utils version 6 package
[AMUTILS].

.ti 0
2. Overview and Rationale

Directory services such as X.500 [X500] or LDAP [RFC2251] are a natural
choice of repository for amd mount map databases. All Object Identifiers
in this document are prefixed by amdSchema-id to be assigned later. The
relation between this schema and the automount schema elements in [HOWARD]
are mostly superficial. The model for the elements in [HOWARD] was the SUN
automounter which has quite a different syntax for mount maps. Furthermore
the intended usage of this schema differs from that of [HOWARD] in many
respects.

.ti 0
3. DSA requirements

Directory servers implementing this schema SHOULD maintain the
modifyTimestamp operational attribute. If not the amdMapCacheTtl
attribute SHOULD be set to 0 indicating to clients that caching of
map entries SHOULD be turned off. Clients wishing to use the amdMap
schema MAY use the modifyTimestamp information to set the ttl for
internal caching schemes. A value of 0 for the amdMapCacheTtl must
result in clients turning off any local caching.

.ti 0
4. Syntax definitions

The following attribute syntax is defined in this document:

     amdlocationlist

This syntax represents a amd map value. This is the syntax expressed
in BNF using definitions from [RFC2252]:

     amdlocationlist      = amdlocationselection |
                            amdlocationlist whsp "||" whsp amdlocationselection

     amdlocationselection = amdlocation |
                            amdlocationselection whsp amdlocation

     amdlocation          = amdlocationinfo |
                            "-" amdlocationinfo |
                            "-"

     amdlocationinfo      = seloropt |
                            amdlocationinfo ";" seloropt |
                            ";"

     seloropt             = seletion |
                            optass

     selection            = keystring "==" printablestring
                            keystring "!=" printablestring

     optass               = keystring \":=\" printablestring

X.500 servers or LDAPv3 servers (supporting the binary attribute
option) may use the following syntax definition:

     AmdLocationList ::= SEQUENCE OF {
                            SEQUENCE OF {
                               location     AmdLocation
                            }
                         }

     AmdLocation     ::= SET OF {
                            CHOICE {
                               location     [0] AmdLocationInfo
                               notlocation  [1] AmdLocationInfo
                               not          [2] NULL
                            }
                         }

     AmdLocationInfo ::= SET OF {
                            CHOICE {
                               selection    [0] AmdSelection
                               option       [1] AmdOption
                            }
                         }

     AmdSelection    ::= CHOICE {
                            eq     [0] AttributeAndValue
                            ne     [1] AttributeAndValue
                         }

     AmdOption       ::= AttributeAndValue

     AttributeAndValue ::= SEQUENCE {
                              attribute     IA5String
                              value         IA5String
                           }

.ti 0
5. Attribute types

The following attribute types are defined in this document:

     amdMapName
     amdMapCacheTtl
     amdMapEntry
     amdMapEntryKey
     amdMapEntryValue

     amdSchema-a OBJECT IDENTIFIER ::= { amdSchema-id 1 }

     amdMapName
	ATTRIBUTE ::= {
           WITH SYNTAX              IA5String
           EQUALITY MATCHING RULE   caseIgoreExactMatch
           --ID                     { amdSchema-a 1 }
           DESCRIPTION
            "This attribute is the symbolic and in the naming
             context unique name of an amd map. This corresponds
             in the case of a flat file database to the name of
             the file or the mount-point of the map."
        }


     amdMapCacheTtl
        ATTRIBUTE ::= {
           WITH SYNTAX              Integer
           EQUALITY MATCHING RULE   integerExactMatch
           --ID                     { amdSchema-a 2 }
           SINGLE VALUED
           DESCRIPTION
            "The maximum time-to-live for the entries in this
             map. After this many milliseconds the map has to
             be cleared from local caches and reloaded. A value
             of 0 disables caching."
        }

     amdMapEntry
        ATTRIBUTE ::= {
           WITH SYNTAX              DistinguishedName
           EQUALITY MATHCING RULE   dNCaseIgnoreExactMatch
           --ID                     { amdSchema-a 3 }
           DESCRIPTION
            "A multivalued attribute listing the distinguished
             names of the amdMapEntries making up this amdMap
             object."
        }

     amdMapEntryKey ::= {
        ATTRIBUTE ::= {
           WITH SYNTAX              IA5String
           EQUALITY MATCHING RULE   stringExactMatch
           --ID                     { amdSchema-a 4 }
           SINGLE VALUED
           DESCRIPTION
            "The value of this attribute is usually the name of
             a mountpoint for this amdMapEntry."
        }

     amdMapEntryValue ::= {
        ATTRIBUTE ::= {
           WITH SYNTAX              AmdLocationList
           --ID                     { amdSchema-a 5 }
           DESCRIPTION
            "This is the actual mount information for the amdMapEntry
             using the syntax described above."
        }

     amdMapEntryKey ::= {
        ATTRIBUTE ::= {
           WITH SYNTAX              IA5String
           EQUALITY MATCHING RULE   stringExactMatch
           --ID                     { amdSchema-a 4 }
           SINGLE VALUED
           DESCRIPTION
            "The value of this attribute is usually the name of
             a mountpoint for this amdMapEntry."
        }

     amdMapEntryValue ::= {
        ATTRIBUTE ::= {
           WITH SYNTAX              AmdLocationList
           --ID                     { amdSchema-a 5 }
           DESCRIPTION
            "This is the actual mount information for the amdMapEntry
             using the syntax described above."
        }

.ti 0
6. Object classes

The following object classes are defined in this document:

          amdMap
          amdMapEntry

defined as follows:

          amdSchema-oc ::= { amdSchema-id 2 }

          amdMap OBJECT-CLASS ::= {
                    SUBCLASS OF            { top }
                    KIND                   auxiliary
                    --ID                   { amdSchema-oc 1 }
                    MAY CONTAIN            { amdMapCacheTtl , cn }
                    MUST CONTAIN           { amdMapName , amdMapEntry }
                 }

          amdMapEntry OBJECT-CLASS ::= {
                         SUBCLASS OF       { top }
                         KIND              structural
                         --ID              { amdSchema-oc 2 }
                         MUST CONTAIN      {
                                             amdMapName ,
                                             amdEntryKey ,
                                             amdEntryValue ,
                                           }
	                 MAY CONTAIN       { cn }
	                 DESCRIPTION
		          "An entry of this object class describes mount
	                   information relative to a certain amdMap entry"
                      }

.ti 0
7. Examples



.ti 0
8. Security Considerations

Due to the security problems posed by NFS care should be taken not to
advertise exported filesystems. Therefore it is often desirable to limit
access to entries carrying amd mount map information to those systems
to which the corresponding filesystems have been exported.

.ti 0
9. References

   [AMUTILS]
        am-utils homepage: http://shekel.cs.columbia.edu/~erez/am-utils.html

   [RFC2251]
        M. Wahl, T. Howes, S. Kille, "Lightweight Directory Access
        Protocol (v3)", RFC 2251, December 1997.

   [RFC2252]
        M. Wahl, A. Coulbeck, T. Howes, S. Kille, "Lightweight Directory
        Access Protocol (v3): Attribute Syntax Definitions", RFC 2252,
        December 1997.

   [RFC2253]
        M. Wahl, S. Kille, T. Howes, "Lightweight Directory Access
        Protocol (v3): UTF-8 String Representation of Distinguished
        Names", RFC 2253, December 1997.

   [HOWARD]
        Luke Howard, "An Approach for Using LDAP as a Network
        Information Service", draft-howard-nis-schema-??.txt, Internet
        draft.

   [X500]
        ITU something or other.

.in 3


.ti 0
Author's Address

.nf

Leif Johansson
Department of Mathematics
Stockholm University
S-106 91 Stockholm
         SWEDEN

Email: leifj AT matematik.su.se
