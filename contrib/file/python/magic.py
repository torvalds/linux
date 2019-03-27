# coding: utf-8

'''
Python bindings for libmagic
'''

import ctypes

from collections import namedtuple

from ctypes import *
from ctypes.util import find_library


def _init():
    """
    Loads the shared library through ctypes and returns a library
    L{ctypes.CDLL} instance
    """
    return ctypes.cdll.LoadLibrary(find_library('magic'))

_libraries = {}
_libraries['magic'] = _init()

# Flag constants for open and setflags
MAGIC_NONE = NONE = 0
MAGIC_DEBUG = DEBUG = 1
MAGIC_SYMLINK = SYMLINK = 2
MAGIC_COMPRESS = COMPRESS = 4
MAGIC_DEVICES = DEVICES = 8
MAGIC_MIME_TYPE = MIME_TYPE = 16
MAGIC_CONTINUE = CONTINUE = 32
MAGIC_CHECK = CHECK = 64
MAGIC_PRESERVE_ATIME = PRESERVE_ATIME = 128
MAGIC_RAW = RAW = 256
MAGIC_ERROR = ERROR = 512
MAGIC_MIME_ENCODING = MIME_ENCODING = 1024
MAGIC_MIME = MIME = 1040  # MIME_TYPE + MIME_ENCODING
MAGIC_APPLE = APPLE = 2048

MAGIC_NO_CHECK_COMPRESS = NO_CHECK_COMPRESS = 4096
MAGIC_NO_CHECK_TAR = NO_CHECK_TAR = 8192
MAGIC_NO_CHECK_SOFT = NO_CHECK_SOFT = 16384
MAGIC_NO_CHECK_APPTYPE = NO_CHECK_APPTYPE = 32768
MAGIC_NO_CHECK_ELF = NO_CHECK_ELF = 65536
MAGIC_NO_CHECK_TEXT = NO_CHECK_TEXT = 131072
MAGIC_NO_CHECK_CDF = NO_CHECK_CDF = 262144
MAGIC_NO_CHECK_TOKENS = NO_CHECK_TOKENS = 1048576
MAGIC_NO_CHECK_ENCODING = NO_CHECK_ENCODING = 2097152

MAGIC_NO_CHECK_BUILTIN = NO_CHECK_BUILTIN = 4173824

FileMagic = namedtuple('FileMagic', ('mime_type', 'encoding', 'name'))


class magic_set(Structure):
    pass
magic_set._fields_ = []
magic_t = POINTER(magic_set)

_open = _libraries['magic'].magic_open
_open.restype = magic_t
_open.argtypes = [c_int]

_close = _libraries['magic'].magic_close
_close.restype = None
_close.argtypes = [magic_t]

_file = _libraries['magic'].magic_file
_file.restype = c_char_p
_file.argtypes = [magic_t, c_char_p]

_descriptor = _libraries['magic'].magic_descriptor
_descriptor.restype = c_char_p
_descriptor.argtypes = [magic_t, c_int]

_buffer = _libraries['magic'].magic_buffer
_buffer.restype = c_char_p
_buffer.argtypes = [magic_t, c_void_p, c_size_t]

_error = _libraries['magic'].magic_error
_error.restype = c_char_p
_error.argtypes = [magic_t]

_setflags = _libraries['magic'].magic_setflags
_setflags.restype = c_int
_setflags.argtypes = [magic_t, c_int]

_load = _libraries['magic'].magic_load
_load.restype = c_int
_load.argtypes = [magic_t, c_char_p]

_compile = _libraries['magic'].magic_compile
_compile.restype = c_int
_compile.argtypes = [magic_t, c_char_p]

_check = _libraries['magic'].magic_check
_check.restype = c_int
_check.argtypes = [magic_t, c_char_p]

_list = _libraries['magic'].magic_list
_list.restype = c_int
_list.argtypes = [magic_t, c_char_p]

_errno = _libraries['magic'].magic_errno
_errno.restype = c_int
_errno.argtypes = [magic_t]


class Magic(object):
    def __init__(self, ms):
        self._magic_t = ms

    def close(self):
        """
        Closes the magic database and deallocates any resources used.
        """
        _close(self._magic_t)

    @staticmethod
    def __tostr(s):
        if s is None:
            return None
        if isinstance(s, str):
            return s
        try:  # keep Python 2 compatibility
            return str(s, 'utf-8')
        except TypeError:
            return str(s)

    @staticmethod
    def __tobytes(b):
        if b is None:
            return None
        if isinstance(b, bytes):
            return b
        try:  # keep Python 2 compatibility
            return bytes(b, 'utf-8')
        except TypeError:
            return bytes(b)

    def file(self, filename):
        """
        Returns a textual description of the contents of the argument passed
        as a filename or None if an error occurred and the MAGIC_ERROR flag
        is set. A call to errno() will return the numeric error code.
        """
        return Magic.__tostr(_file(self._magic_t, Magic.__tobytes(filename)))

    def descriptor(self, fd):
        """
        Returns a textual description of the contents of the argument passed
        as a file descriptor or None if an error occurred and the MAGIC_ERROR
        flag is set. A call to errno() will return the numeric error code.
        """
        return Magic.__tostr(_descriptor(self._magic_t, fd))

    def buffer(self, buf):
        """
        Returns a textual description of the contents of the argument passed
        as a buffer or None if an error occurred and the MAGIC_ERROR flag
        is set. A call to errno() will return the numeric error code.
        """
        return Magic.__tostr(_buffer(self._magic_t, buf, len(buf)))

    def error(self):
        """
        Returns a textual explanation of the last error or None
        if there was no error.
        """
        return Magic.__tostr(_error(self._magic_t))

    def setflags(self, flags):
        """
        Set flags on the magic object which determine how magic checking
        behaves; a bitwise OR of the flags described in libmagic(3), but
        without the MAGIC_ prefix.

        Returns -1 on systems that don't support utime(2) or utimes(2)
        when PRESERVE_ATIME is set.
        """
        return _setflags(self._magic_t, flags)

    def load(self, filename=None):
        """
        Must be called to load entries in the colon separated list of database
        files passed as argument or the default database file if no argument
        before any magic queries can be performed.

        Returns 0 on success and -1 on failure.
        """
        return _load(self._magic_t, Magic.__tobytes(filename))

    def compile(self, dbs):
        """
        Compile entries in the colon separated list of database files
        passed as argument or the default database file if no argument.
        The compiled files created are named from the basename(1) of each file
        argument with ".mgc" appended to it.

        Returns 0 on success and -1 on failure.
        """
        return _compile(self._magic_t, Magic.__tobytes(dbs))

    def check(self, dbs):
        """
        Check the validity of entries in the colon separated list of
        database files passed as argument or the default database file
        if no argument.

        Returns 0 on success and -1 on failure.
        """
        return _check(self._magic_t, Magic.__tobytes(dbs))

    def list(self, dbs):
        """
        Check the validity of entries in the colon separated list of
        database files passed as argument or the default database file
        if no argument.

        Returns 0 on success and -1 on failure.
        """
        return _list(self._magic_t, Magic.__tobytes(dbs))

    def errno(self):
        """
        Returns a numeric error code. If return value is 0, an internal
        magic error occurred. If return value is non-zero, the value is
        an OS error code. Use the errno module or os.strerror() can be used
        to provide detailed error information.
        """
        return _errno(self._magic_t)


def open(flags):
    """
    Returns a magic object on success and None on failure.
    Flags argument as for setflags.
    """
    return Magic(_open(flags))


# Objects used by `detect_from_` functions
mime_magic = Magic(_open(MAGIC_MIME))
mime_magic.load()
none_magic = Magic(_open(MAGIC_NONE))
none_magic.load()


def _create_filemagic(mime_detected, type_detected):
    try:
        mime_type, mime_encoding = mime_detected.split('; ')
    except ValueError:
        raise ValueError(mime_detected)

    return FileMagic(name=type_detected, mime_type=mime_type,
                     encoding=mime_encoding.replace('charset=', ''))


def detect_from_filename(filename):
    '''Detect mime type, encoding and file type from a filename

    Returns a `FileMagic` namedtuple.
    '''

    return _create_filemagic(mime_magic.file(filename),
                             none_magic.file(filename))


def detect_from_fobj(fobj):
    '''Detect mime type, encoding and file type from file-like object

    Returns a `FileMagic` namedtuple.
    '''

    file_descriptor = fobj.fileno()
    return _create_filemagic(mime_magic.descriptor(file_descriptor),
                             none_magic.descriptor(file_descriptor))


def detect_from_content(byte_content):
    '''Detect mime type, encoding and file type from bytes

    Returns a `FileMagic` namedtuple.
    '''

    return _create_filemagic(mime_magic.buffer(byte_content),
                             none_magic.buffer(byte_content))
