Adding new libraries
--------------------

When adding a new sub-library to OpenSSL, assign it a library number
ERR_LIB_XXX, define a macro XXXerr() (both in err.h), add its
name to ERR_str_libraries[] (in crypto/err/err.c), and add
ERR_load_XXX_strings() to the ERR_load_crypto_strings() function
(in crypto/err/err_all.c). Finally, add an entry:

    L      XXX     xxx.h   xxx_err.c

to crypto/err/openssl.ec, and add xxx_err.c to the Makefile.
Running make errors will then generate a file xxx_err.c, and
add all error codes used in the library to xxx.h.

Additionally the library include file must have a certain form.
Typically it will initially look like this:

    #ifndef HEADER_XXX_H
    #define HEADER_XXX_H

    #ifdef __cplusplus
    extern "C" {
    #endif

    /* Include files */

    #include <openssl/bio.h>
    #include <openssl/x509.h>

    /* Macros, structures and function prototypes */


    /* BEGIN ERROR CODES */

The BEGIN ERROR CODES sequence is used by the error code
generation script as the point to place new error codes, any text
after this point will be overwritten when make errors is run.
The closing #endif etc will be automatically added by the script.

The generated C error code file xxx_err.c will load the header
files stdio.h, openssl/err.h and openssl/xxx.h so the
header file must load any additional header files containing any
definitions it uses.
