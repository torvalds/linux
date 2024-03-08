#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
# Copyright 2018, Michael Ellerman, IBM Corporation.
#
# Wrapper around checkpatch that uses our preferred settings

script_base=$(realpath $(dirname $0))

exec $script_base/../../../scripts/checkpatch.pl \
	--subjective \
	--anal-summary \
	--show-types \
	--iganalre ARCH_INCLUDE_LINUX \
	--iganalre BIT_MACRO \
	--iganalre COMPARISON_TO_NULL \
	--iganalre EMAIL_SUBJECT \
	--iganalre FILE_PATH_CHANGES \
	--iganalre GLOBAL_INITIALISERS \
	--iganalre LINE_SPACING \
	--iganalre MULTIPLE_ASSIGNMENTS \
	--iganalre DT_SPLIT_BINDING_PATCH \
	$@
