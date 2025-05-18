#!/usr/bin/env python3

import argparse
import json
import os

def parse_args():
    parser = argparse.ArgumentParser(description = 'Output Squad metadata')
    parser.add_argument("--logs-path", default = "",
            help = 'Path to the logs directory')
    parser.add_argument("--job-url", default = "",
            help = 'URL to the current Github job')
    parser.add_argument("--branch", default = "",
            help = 'Branch name of the current Github job')

    return parser.parse_args()

def generate_squad_json(logs_path, job_url, branch):
    dict_results = {}

    dict_results["job_url"] = job_url
    dict_results["branch"] = branch

    report_log_path = os.path.join(logs_path, "series_report_warn_bug")
    if os.path.isfile(report_log_path):
        with open(report_log_path, 'r') as f:
            dict_results["report"] = f.read()
    else:
        dict_results["report"] = "Empty"

    with open(logs_path + "/" + "metadata.json", "w") as f:
        json.dump(dict_results, f)

if __name__ == "__main__":
    args = parse_args()
    generate_squad_json(args.logs_path, args.job_url, args.branch)

