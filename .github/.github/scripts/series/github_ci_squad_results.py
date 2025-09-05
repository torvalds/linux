#!/usr/bin/env python3

import argparse
import json

def parse_args():
    parser = argparse.ArgumentParser(description = 'Output Squad tests results for the Github CI')
    parser.add_argument("--logs-path", default = "",
            help = 'Path to the log files')

    return parser.parse_args()

def generate_squad_json(logs_path):
    dict_results = {}

    with open(logs_path + "/series.log", "r") as f:
        logs_content = f.readlines()

    for line in logs_content:
        # ::notice::OK Build kernel rv64__nommu_k210_sdcard_defconfig__plain__gcc took 15.74s
        if not line.startswith("::notice::") and not line.startswith("::error::"):
            continue

        # We parse only the Builds to get the build name and then
        # we add all the corresponding tests

        # Either "Build" or "Test"
        type_result = line.split(" ")[1]
        if type_result == "Test":
            break

        build_name = line.split(" ")[3]
        time = line.split(" ")[-1]

        with open(logs_path + "/build_kernel___" + build_name + ".log", "r") as f:
            build_content = f.read()

        if line.split(" ")[0] == "::error::FAIL":
            dict_results[build_name + "/build"] = { "result": "fail", "time": time, "log": build_content }
            continue

        dict_results[build_name + "/build"] = { "result": "pass", "time": time, "log": build_content }

        # The build succeeded, so look for the associated tests
        for test_line in logs_content:
            if not test_line.startswith("::notice::") and not test_line.startswith("::error::"):
                continue

            type_result = test_line.split(" ")[1]
            if type_result == "Build":
                continue

            test_build_name = test_line.split(" ")[3]
            if test_build_name != build_name:
                continue

            rootfs = test_line.split(" ")[4]
            test_type = test_line.split(" ")[5] + "__" + test_line.split(" ")[6] + "__" + test_line.split(" ")[7] + "__" + test_line.split(" ")[8]

            log_name = "test_kernel" + "___" + test_build_name + "___" + rootfs + "___" + test_type + ".log"
            with open(logs_path + "/" + log_name, "r") as f:
                log_content = f.read()

            if test_line.split(" ")[0] == "::error::FAIL":
                result = "fail"
            else:
                result = "pass"

            dict_results[build_name + "/" + rootfs + "___" + test_type] = { "result": result, "log": log_content }


    with open(logs_path + "/squad.json", "w") as f:
        json.dump(dict_results, f)

if __name__ == "__main__":
    args = parse_args()
    generate_squad_json(args.logs_path)

