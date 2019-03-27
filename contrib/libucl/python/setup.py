try:
    from setuptools import setup, Extension
except ImportError:
    from distutils.core import setup, Extension

import os
import sys

tests_require = []

if sys.version < '2.7':
    tests_require.append('unittest2')

uclmodule = Extension(
    'ucl',
    libraries = ['ucl'],
    sources = ['src/uclmodule.c'],
    language = 'c'
)

setup(
    name = 'ucl',
    version = '0.8',
    description = 'ucl parser and emmitter',
    ext_modules = [uclmodule],
    test_suite = 'tests',
    tests_require = tests_require,
    author = "Eitan Adler, Denis Volpato Martins",
    author_email = "lists@eitanadler.com",
    url = "https://github.com/vstakhov/libucl/",
    license = "MIT",
    classifiers = [
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "License :: DFSG approved",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: C",
        "Programming Language :: Python :: 2",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: Implementation :: CPython",
        "Topic :: Software Development :: Libraries",
    ]
)
