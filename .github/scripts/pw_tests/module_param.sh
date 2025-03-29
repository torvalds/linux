#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2020 Facebook

params=$(git show | grep -i '^\+.*module_param')
new_params=$(git show | grep -ic '^\+.*module_param')
old_params=$(git show | grep -ic '^\-.*module_param')

echo "Was $old_params now: $new_params"

if [ -z "$params" ]; then
        exit 0
fi

echo -e "Detected module_param\n$params"
if [ $new_params -eq $old_params ]; then
        exit 250
fi

exit 1
