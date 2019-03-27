/*
 * Copyright 2011-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifdef NAML$C_MAXRSS

# define CC_RMS_NAMX cc$rms_naml
# define FAB_NAMX fab$l_naml
# define FAB_OR_NAML( fab, naml) naml
# define FAB_OR_NAML_DNA naml$l_long_defname
# define FAB_OR_NAML_DNS naml$l_long_defname_size
# define FAB_OR_NAML_FNA naml$l_long_filename
# define FAB_OR_NAML_FNS naml$l_long_filename_size
# define NAMX_ESA naml$l_long_expand
# define NAMX_ESL naml$l_long_expand_size
# define NAMX_ESS naml$l_long_expand_alloc
# define NAMX_NOP naml$b_nop
# define SET_NAMX_NO_SHORT_UPCASE( nam) nam.naml$v_no_short_upcase = 1

# if __INITIAL_POINTER_SIZE == 64
#  define NAMX_DNA_FNA_SET(fab) fab.fab$l_dna = (__char_ptr32) -1; \
   fab.fab$l_fna = (__char_ptr32) -1;
# else                          /* __INITIAL_POINTER_SIZE == 64 */
#  define NAMX_DNA_FNA_SET(fab) fab.fab$l_dna = (char *) -1; \
   fab.fab$l_fna = (char *) -1;
# endif                         /* __INITIAL_POINTER_SIZE == 64 [else] */

# define NAMX_MAXRSS NAML$C_MAXRSS
# define NAMX_STRUCT NAML

#else                           /* def NAML$C_MAXRSS */

# define CC_RMS_NAMX cc$rms_nam
# define FAB_NAMX fab$l_nam
# define FAB_OR_NAML( fab, naml) fab
# define FAB_OR_NAML_DNA fab$l_dna
# define FAB_OR_NAML_DNS fab$b_dns
# define FAB_OR_NAML_FNA fab$l_fna
# define FAB_OR_NAML_FNS fab$b_fns
# define NAMX_ESA nam$l_esa
# define NAMX_ESL nam$b_esl
# define NAMX_ESS nam$b_ess
# define NAMX_NOP nam$b_nop
# define NAMX_DNA_FNA_SET(fab)
# define NAMX_MAXRSS NAM$C_MAXRSS
# define NAMX_STRUCT NAM
# ifdef NAM$M_NO_SHORT_UPCASE
#  define SET_NAMX_NO_SHORT_UPCASE( nam) naml.naml$v_no_short_upcase = 1
# else                          /* def NAM$M_NO_SHORT_UPCASE */
#  define SET_NAMX_NO_SHORT_UPCASE( nam)
# endif                         /* def NAM$M_NO_SHORT_UPCASE [else] */

#endif                          /* def NAML$C_MAXRSS [else] */
