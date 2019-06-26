#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
# Copyright 2018, Michael Ellerman, IBM Corporation.
#
# Wrapper around checkpatch that uses our preferred settings

script_base=$(realpath $(dirname $0))

exec $script_base/../../../scripts/checkpatch.pl \
	--subjective \
	--no-summary \
	--max-line-length=90 \
	--show-types \
	--ignore ARCH_INCLUDE_LINUX \
	--ignore BIT_MACRO \
	--ignore COMPARISON_TO_NULL \
	--ignore EMAIL_SUBJECT \
	--ignore FILE_PATH_CHANGES \
	--ignore GLOBAL_INITIALISERS \
	--ignore LINE_SPACING \
	--ignore MULTIPLE_ASSIGNMENTS \
	--ignore DT_SPLIT_BINDING_PATCH \
	$@
