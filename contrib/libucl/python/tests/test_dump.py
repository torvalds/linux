from .compat import unittest
import ucl
import sys

class DumpTest(unittest.TestCase):
    def test_no_args(self):
        with self.assertRaises(TypeError):
            ucl.dump()

    def test_none(self):
        self.assertEqual(ucl.dump(None), None)

    def test_null(self):
        data = { "a" : None }
        valid = "a = null;\n"
        self.assertEqual(ucl.dump(data), valid)

    def test_int(self):
        data = { "a" : 1 }
        valid = "a = 1;\n"
        self.assertEqual(ucl.dump(data), valid)

    def test_nested_int(self):
        data = { "a" : { "b" : 1 } }
        valid = "a {\n    b = 1;\n}\n"
        self.assertEqual(ucl.dump(data), valid)

    def test_int_array(self):
        data = { "a" : [1,2,3,4] }
        valid = "a [\n    1,\n    2,\n    3,\n    4,\n]\n"
        self.assertEqual(ucl.dump(data), valid)

    def test_str(self):
        data = { "a" : "b" }
        valid = "a = \"b\";\n"
        self.assertEqual(ucl.dump(data), valid)

    @unittest.skipIf(sys.version_info[0] > 2, "Python3 uses unicode only")
    def test_unicode(self):
        data = { unicode("a") : unicode("b") }
        valid = unicode("a = \"b\";\n")
        self.assertEqual(ucl.dump(data), valid)

    def test_float(self):
        data = { "a" : 1.1 }
        valid = "a = 1.100000;\n"
        self.assertEqual(ucl.dump(data), valid)

    def test_boolean(self):
        data = { "a" : True, "b" : False }
        valid = [
            "a = true;\nb = false;\n",
            "b = false;\na = true;\n"
            ]
        self.assertIn(ucl.dump(data), valid)

    def test_empty_ucl(self):
        self.assertEqual(ucl.dump({}), "")

    def test_json(self):
        data = { "a" : 1, "b": "bleh;" }
        valid = [
            '{\n    "a": 1,\n    "b": "bleh;"\n}',
            '{\n    "b": "bleh;",\n    "a": 1\n}'
            ]
        self.assertIn(ucl.dump(data, ucl.UCL_EMIT_JSON), valid)
