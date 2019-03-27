divert(-1)
#
# Copyright (c) 2014 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

divert(-1)
# Arguments:
# 1: prefix to match; must be one or more tokens
#    (this is not a "substring" match)
# 2: flags to set
# NYI: 3: replacement for 1 (empty for now)

ifelse(defn(`_ARG_'), `', `errprint(`Feature "prefixmod" requires argument')',
	`define(`_PREFIX_MOD_', _ARG_)')
ifelse(len(X`'_ARG2_),`1', `errprint(`Feature "prefixmod" requires two arguments')',
	`define(`_PREFIX_FLAGS_', _ARG2_)')

define(`_NEED_MACRO_MAP_', `1')
