#!/usr/bin/env python3

import argparse
from collections import defaultdict
import difflib
import os
import re
from glcollate import Collate
from termcolor import colored
from urllib.parse import urlparse


def get_canonical_name(job_name):
    return re.split(r" \d+/\d+", job_name)[0]


def get_xfails_file_path(job_name, suffix):
    canonical_name = get_canonical_name(job_name)
    name = canonical_name.replace(":", "-")
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(script_dir, f"{name}-{suffix}.txt")


def get_unit_test_name_and_results(unit_test):
    if "Artifact results/failures.csv not found" in unit_test or '' == unit_test:
        return None, None
    unit_test_name, unit_test_result = unit_test.strip().split(",")
    return unit_test_name, unit_test_result


def read_file(file_path):
    try:
        with open(file_path, "r") as file:
            f = file.readlines()
            if len(f):
                f[-1] = f[-1].strip() + "\n"
            return f
    except FileNotFoundError:
        return []


def save_file(content, file_path):
    # delete file is content is empty
    if not content or not any(content):
        if os.path.exists(file_path):
            os.remove(file_path)
        return

    with open(file_path, "w") as file:
        file.writelines(content)


def is_test_present_on_file(file_content, unit_test_name):
    return any(unit_test_name in line for line in file_content)


def is_unit_test_present_in_other_jobs(unit_test, job_ids):
    return all(unit_test in job_ids[job_id] for job_id in job_ids)


def remove_unit_test_if_present(lines, unit_test_name):
    if not is_test_present_on_file(lines, unit_test_name):
        return
    lines[:] = [line for line in lines if unit_test_name not in line]


def add_unit_test_if_not_present(lines, unit_test_name, file_name):
    # core_getversion is mandatory
    if "core_getversion" in unit_test_name:
        print("WARNING: core_getversion should pass, not adding it to", os.path.basename(file_name))
    elif all(unit_test_name not in line for line in lines):
        lines.append(unit_test_name + "\n")


def update_unit_test_result_in_fails_txt(fails_txt, unit_test):
    unit_test_name, unit_test_result = get_unit_test_name_and_results(unit_test)
    for i, line in enumerate(fails_txt):
        if unit_test_name in line:
            _, current_result = get_unit_test_name_and_results(line)
            fails_txt[i] = unit_test + "\n"
            return


def add_unit_test_or_update_result_to_fails_if_present(fails_txt, unit_test, fails_txt_path):
    unit_test_name, _ = get_unit_test_name_and_results(unit_test)
    if not is_test_present_on_file(fails_txt, unit_test_name):
        add_unit_test_if_not_present(fails_txt, unit_test, fails_txt_path)
    # if it is present but not with the same result
    elif not is_test_present_on_file(fails_txt, unit_test):
        update_unit_test_result_in_fails_txt(fails_txt, unit_test)


def split_unit_test_from_collate(xfails):
    for job_name in xfails.keys():
        for job_id in xfails[job_name].copy().keys():
            if "not found" in xfails[job_name][job_id].content_as_str:
                del xfails[job_name][job_id]
                continue
            xfails[job_name][job_id] = xfails[job_name][job_id].content_as_str.splitlines()


def get_xfails_from_pipeline_url(pipeline_url):
    parsed_url = urlparse(pipeline_url)
    path_components = parsed_url.path.strip("/").split("/")

    namespace = path_components[0]
    project = path_components[1]
    pipeline_id = path_components[-1]

    print("Collating from:", namespace, project, pipeline_id)
    xfails = (
        Collate(namespace=namespace, project=project)
        .from_pipeline(pipeline_id)
        .get_artifact("results/failures.csv")
    )

    split_unit_test_from_collate(xfails)
    return xfails


def get_xfails_from_pipeline_urls(pipelines_urls):
    xfails = defaultdict(dict)

    for url in pipelines_urls:
        new_xfails = get_xfails_from_pipeline_url(url)
        for key in new_xfails:
            xfails[key].update(new_xfails[key])

    return xfails


def print_diff(old_content, new_content, file_name):
    diff = difflib.unified_diff(old_content, new_content, lineterm="", fromfile=file_name, tofile=file_name)
    diff = [colored(line, "green") if line.startswith("+") else
            colored(line, "red") if line.startswith("-") else line for line in diff]
    print("\n".join(diff[:3]))
    print("".join(diff[3:]))


def main(pipelines_urls, only_flakes):
    xfails = get_xfails_from_pipeline_urls(pipelines_urls)

    for job_name in xfails.keys():
        fails_txt_path = get_xfails_file_path(job_name, "fails")
        flakes_txt_path = get_xfails_file_path(job_name, "flakes")

        fails_txt = read_file(fails_txt_path)
        flakes_txt = read_file(flakes_txt_path)

        fails_txt_original = fails_txt.copy()
        flakes_txt_original = flakes_txt.copy()

        for job_id in xfails[job_name].keys():
            for unit_test in xfails[job_name][job_id]:
                unit_test_name, unit_test_result = get_unit_test_name_and_results(unit_test)

                if not unit_test_name:
                    continue

                if only_flakes:
                    remove_unit_test_if_present(fails_txt, unit_test_name)
                    add_unit_test_if_not_present(flakes_txt, unit_test_name, flakes_txt_path)
                    continue

                # drop it from flakes if it is present to analyze it again
                remove_unit_test_if_present(flakes_txt, unit_test_name)

                if unit_test_result == "UnexpectedPass":
                    remove_unit_test_if_present(fails_txt, unit_test_name)
                    # flake result
                    if not is_unit_test_present_in_other_jobs(unit_test, xfails[job_name]):
                        add_unit_test_if_not_present(flakes_txt, unit_test_name, flakes_txt_path)
                    continue

                # flake result
                if not is_unit_test_present_in_other_jobs(unit_test, xfails[job_name]):
                    remove_unit_test_if_present(fails_txt, unit_test_name)
                    add_unit_test_if_not_present(flakes_txt, unit_test_name, flakes_txt_path)
                    continue

                # consistent result
                add_unit_test_or_update_result_to_fails_if_present(fails_txt, unit_test,
                                                                   fails_txt_path)

        fails_txt.sort()
        flakes_txt.sort()

        if fails_txt != fails_txt_original:
            save_file(fails_txt, fails_txt_path)
            print_diff(fails_txt_original, fails_txt, os.path.basename(fails_txt_path))
        if flakes_txt != flakes_txt_original:
            save_file(flakes_txt, flakes_txt_path)
            print_diff(flakes_txt_original, flakes_txt, os.path.basename(flakes_txt_path))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Update xfails from a given pipeline.")
    parser.add_argument("pipeline_urls", nargs="+", type=str, help="URLs to the pipelines to analyze the failures.")
    parser.add_argument("--only-flakes", action="store_true", help="Treat every detected failure as a flake, edit *-flakes.txt only.")

    args = parser.parse_args()

    main(args.pipeline_urls, args.only_flakes)
    print("Done.")
