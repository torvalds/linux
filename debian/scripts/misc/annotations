#!/usr/bin/env python3
# -*- mode: python -*-
# Manage Ubuntu kernel .config and annotations
# Copyright © 2022 Canonical Ltd.

import sys
sys.dont_write_bytecode = True
import os
import argparse
import json
from signal import signal, SIGPIPE, SIG_DFL

from kconfig.annotations import Annotation, KConfig

VERSION = '0.1'

SKIP_CONFIGS = (
    # CONFIG_VERSION_SIGNATURE is dynamically set during the build
    'CONFIG_VERSION_SIGNATURE',
    # Allow to use a different versions of toolchain tools
    'CONFIG_GCC_VERSION',
    'CONFIG_CC_VERSION_TEXT',
    'CONFIG_AS_VERSION',
    'CONFIG_LD_VERSION',
    'CONFIG_LLD_VERSION',
    'CONFIG_CLANG_VERSION',
    'CONFIG_PAHOLE_VERSION',
    'CONFIG_RUSTC_VERSION_TEXT',
    'CONFIG_BINDGEN_VERSION_TEXT',
)


def make_parser():
    parser = argparse.ArgumentParser(
        description='Manage Ubuntu kernel .config and annotations',
    )
    parser.add_argument('--version', '-v', action='version', version=f'%(prog)s {VERSION}')

    parser.add_argument('--file', '-f', action='store',
                        help='Pass annotations or .config file to be parsed')
    parser.add_argument('--arch', '-a', action='store',
                        help='Select architecture')
    parser.add_argument('--flavour', '-l', action='store',
                        help='Select flavour (default is "generic")')
    parser.add_argument('--config', '-c', action='store',
                        help='Select a specific config option')
    parser.add_argument('--query', '-q', action='store_true',
                        help='Query annotations')
    parser.add_argument('--note', '-n', action='store',
                        help='Write a specific note to a config option in annotations')
    parser.add_argument('--autocomplete', action='store_true',
                        help='Enable config bash autocomplete: `source <(annotations --autocomplete)`')
    parser.add_argument('--source', '-t', action='store_true',
                        help='Jump to a config definition in the kernel source code')

    ga = parser.add_argument_group(title='Action').add_mutually_exclusive_group(required=False)
    ga.add_argument('--write', '-w', action='store',
                    metavar='VALUE', dest='value',
                    help='Set a specific config value in annotations (use \'null\' to remove)')
    ga.add_argument('--export', '-e', action='store_true',
                    help='Convert annotations to .config format')
    ga.add_argument('--import', '-i', action='store',
                    metavar="FILE", dest='import_file',
                    help='Import a full .config for a specific arch and flavour into annotations')
    ga.add_argument('--update', '-u', action='store',
                    metavar="FILE", dest='update_file',
                    help='Import a partial .config into annotations (only resync configs specified in FILE)')
    ga.add_argument('--check', '-k', action='store',
                    metavar="FILE", dest='check_file',
                    help='Validate kernel .config with annotations')
    return parser


_ARGPARSER = make_parser()


def arg_fail(message):
    print(message)
    _ARGPARSER.print_usage()
    sys.exit(1)


def print_result(config, res):
    if res is not None and config not in res:
        res = {config or '*': res}
    print(json.dumps(res, indent=4))


def do_query(args):
    if args.arch is None and args.flavour is not None:
        arg_fail('error: --flavour requires --arch')
    a = Annotation(args.file)
    res = a.search_config(config=args.config, arch=args.arch, flavour=args.flavour)
    print_result(args.config, res)


def do_autocomplete(args):
    a = Annotation(args.file)
    res = (c.removeprefix('CONFIG_') for c in a.search_config())
    res_str = ' '.join(res)
    print(f'complete -W "{res_str}" annotations')


def do_source(args):
    if args.config is None:
        arg_fail('error: --source requires --config')
    if not os.path.exists('tags'):
        print('tags not found in the current directory, try: `make tags`')
        sys.exit(1)
    os.system(f'vim -t {args.config}')


def do_note(args):
    if args.config is None:
        arg_fail('error: --note requires --config')

    # Set the note in annotations
    a = Annotation(args.file)
    a.set(args.config, note=args.note)

    # Save back to annotations
    a.save(args.file)

    # Query and print back the value
    a = Annotation(args.file)
    res = a.search_config(config=args.config)
    print_result(args.config, res)


def do_write(args):
    if args.config is None:
        arg_fail('error: --write requires --config')

    # Set the value in annotations ('null' means remove)
    a = Annotation(args.file)
    if args.value == 'null':
        a.remove(args.config, arch=args.arch, flavour=args.flavour)
    else:
        a.set(args.config, arch=args.arch, flavour=args.flavour, value=args.value, note=args.note)

    # Save back to annotations
    a.save(args.file)

    # Query and print back the value
    a = Annotation(args.file)
    res = a.search_config(config=args.config)
    print_result(args.config, res)


def do_export(args):
    if args.arch is None:
        arg_fail('error: --export requires --arch')
    a = Annotation(args.file)
    conf = a.search_config(config=args.config, arch=args.arch, flavour=args.flavour)
    if conf:
        print(a.to_config(conf))


def do_import(args):
    if args.arch is None:
        arg_fail('error: --arch is required with --import')
    if args.flavour is None:
        arg_fail('error: --flavour is required with --import')
    if args.config is not None:
        arg_fail('error: --config cannot be used with --import (try --update)')

    # Merge with the current annotations
    a = Annotation(args.file)
    c = KConfig(args.import_file)
    a.update(c, arch=args.arch, flavour=args.flavour)

    # Save back to annotations
    a.save(args.file)


def do_update(args):
    if args.arch is None:
        arg_fail('error: --arch is required with --update')

    # Merge with the current annotations
    a = Annotation(args.file)
    c = KConfig(args.update_file)
    if args.config is None:
        configs = list(set(c.config.keys()) - set(SKIP_CONFIGS))
    if configs:
        a.update(c, arch=args.arch, flavour=args.flavour, configs=configs)

    # Save back to annotations
    a.save(args.file)


def do_check(args):
    # Determine arch and flavour
    if args.arch is None:
        arg_fail('error: --arch is required with --check')

    print(f"check-config: loading annotations from {args.file}")
    total = good = ret = 0

    # Load annotations settings
    a = Annotation(args.file)
    a_configs = a.search_config(arch=args.arch, flavour=args.flavour).keys()

    # Parse target .config
    c = KConfig(args.check_file)
    c_configs = c.config.keys()

    # Validate .config against annotations
    for conf in sorted(a_configs | c_configs):
        if conf in SKIP_CONFIGS:
            continue
        entry = a.search_config(config=conf, arch=args.arch, flavour=args.flavour)
        expected = entry[conf] if entry else '-'
        value = c.config[conf] if conf in c.config else '-'
        if value != expected:
            policy = a.config[conf] if conf in a.config else 'undefined'
            if 'policy' in policy:
                policy = f"policy<{policy['policy']}>"
            print(f"check-config: FAIL: ({value} != {expected}): {conf} {policy})")
            ret = 1
        else:
            good += 1
        total += 1

    print(f"check-config: {good}/{total} checks passed -- exit {ret}")
    sys.exit(ret)


def autodetect_annotations(args):
    if args.file:
        return
    # If --file/-f isn't specified try to automatically determine the right
    # location of the annotations file looking at debian/debian.env.
    try:
        with open('debian/debian.env', 'rt', encoding='utf-8') as fd:
            args.file = fd.read().rstrip().split('=')[1] + '/config/annotations'
    except (FileNotFoundError, IndexError):
        arg_fail('error: could not determine DEBDIR, try using: --file/-f')


def main():
    # Prevent broken pipe errors when showing output in pipe to other tools
    # (less for example)
    signal(SIGPIPE, SIG_DFL)

    # Main annotations program
    args = _ARGPARSER.parse_args()
    autodetect_annotations(args)

    if args.config and not args.config.startswith('CONFIG_'):
        args.config = 'CONFIG_' + args.config

    if args.value:
        do_write(args)
    elif args.note:
        do_note(args)
    elif args.export:
        do_export(args)
    elif args.import_file:
        do_import(args)
    elif args.update_file:
        do_update(args)
    elif args.check_file:
        do_check(args)
    elif args.autocomplete:
        do_autocomplete(args)
    elif args.source:
        do_source(args)
    else:
        do_query(args)


if __name__ == '__main__':
    main()
