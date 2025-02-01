#!/bin/bash

if [[ ! -f "${BASELINE_PATH}" ]]; then
    echo "# No ${BASELINE_PATH} available" >> "${GITHUB_STEP_SUMMARY}"

    echo "No ${BASELINE_PATH} available"
    echo "Printing veristat results"
    cat "${VERISTAT_OUTPUT}"

    exit
fi

selftests/bpf/veristat \
    --output-format csv \
    --emit file,prog,verdict,states \
    --compare "${BASELINE_PATH}" "${VERISTAT_OUTPUT}" > compare.csv

python3 ./.github/scripts/veristat_compare.py compare.csv
