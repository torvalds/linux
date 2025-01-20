# -*- mode: python -*-
# Misc helpers for Kconfig and annotations
# Copyright © 2023 Canonical Ltd.

import sys


def autodetect_annotations():
    try:
        with open("debian/debian.env", "rt", encoding="utf-8") as fd:
            return fd.read().rstrip().split("=")[1] + "/config/annotations"
    except (FileNotFoundError, IndexError):
        return None


def arg_fail(parser, message, show_usage=True):
    print(message)
    if show_usage:
        parser.print_usage()
    sys.exit(1)
