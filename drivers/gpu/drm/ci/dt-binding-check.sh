#!/bin/bash
# SPDX-License-Identifier: MIT

set -euxo pipefail

VENV_PATH="${VENV_PATH:-/tmp/dtschema-venv}"
source "${VENV_PATH}/bin/activate"

if ! make -j"${FDO_CI_CONCURRENT:-4}" dt_binding_check \
        DT_SCHEMA_FILES="${SCHEMA:-}" 2>dt-binding-check.log; then
    echo "ERROR: 'make dt_binding_check' failed. Please check dt-binding-check.log for details."
    exit 1
fi

if [[ -s dt-binding-check.log ]]; then
    echo "WARNING: dt_binding_check reported warnings. Please check dt-binding-check.log" \
         "for details."
    exit 102
fi
