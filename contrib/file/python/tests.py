# coding: utf-8

import unittest

import magic


class MagicTestCase(unittest.TestCase):

    filename = 'magic.py'
    expected_mime_type = 'text/x-python'
    expected_encoding = 'us-ascii'
    expected_name = 'Python script, ASCII text executable'

    def assert_result(self, result):
        self.assertEqual(result.mime_type, self.expected_mime_type)
        self.assertEqual(result.encoding, self.expected_encoding)
        self.assertEqual(result.name, self.expected_name)

    def test_detect_from_filename(self):
        result = magic.detect_from_filename(self.filename)
        self.assert_result(result)

    def test_detect_from_fobj(self):
        with open(self.filename) as fobj:
            result = magic.detect_from_fobj(fobj)
        self.assert_result(result)

    def test_detect_from_content(self):
        with open(self.filename) as fobj:
            result = magic.detect_from_content(fobj.read(4096))
        self.assert_result(result)
