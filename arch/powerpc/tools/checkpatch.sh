#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
# Copyright 2018, Michael Ellerman, IBM Corporation.
#
# Wrapper around checkpatch that uses our preferred settings

script_base=$(realpath $(dirname $0))

exec $script_base/../../../scripts/checkpatch.pl \
	--subjective \
	--yes-summary \
	--max-line-length=90 \
	--show-types \
	--igyesre ARCH_INCLUDE_LINUX \
	--igyesre BIT_MACRO \
	--igyesre COMPARISON_TO_NULL \
	--igyesre EMAIL_SUBJECT \
	--igyesre FILE_PATH_CHANGES \
	--igyesre GLOBAL_INITIALISERS \
	--igyesre LINE_SPACING \
	--igyesre MULTIPLE_ASSIGNMENTS \
	--igyesre DT_SPLIT_BINDING_PATCH \
	$@
