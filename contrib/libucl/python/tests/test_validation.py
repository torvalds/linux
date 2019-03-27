from .compat import unittest
import ucl
import json
import os.path
import glob
import re

TESTS_SCHEMA_FOLDER = '../tests/schema/*.json'

comment_re = re.compile('\/\*((?!\*\/).)*?\*\/', re.DOTALL | re.MULTILINE)
def json_remove_comments(content):
    return comment_re.sub('', content)

class ValidationTest(unittest.TestCase):
    def validate(self, jsonfile):
        def perform_test(schema, data, valid, description):
            msg = '%s (valid=%r)' % (description, valid)
            if valid:
                self.assertTrue(ucl.validate(schema, data), msg)
            else:
                with self.assertRaises(ucl.SchemaError):
                    ucl.validate(schema, data)
                    self.fail(msg) # fail() will be called only if SchemaError is not raised

        with open(jsonfile) as f:
            try:
                # data = json.load(f)
                data = json.loads(json_remove_comments(f.read()))
            except ValueError as e:
                raise self.skipTest('Failed to load JSON: %s' % str(e))

            for testgroup in data:
                for test in testgroup['tests']:
                    perform_test(testgroup['schema'], test['data'],
                        test['valid'], test['description'])

    @classmethod
    def setupValidationTests(cls):
        """Creates each test dynamically from a folder"""
        def test_gen(filename):
            def run_test(self):
                self.validate(filename)
            return run_test

        for jsonfile in glob.glob(TESTS_SCHEMA_FOLDER):
            testname = os.path.splitext(os.path.basename(jsonfile))[0]
            setattr(cls, 'test_%s' % testname, test_gen(jsonfile))


ValidationTest.setupValidationTests()
