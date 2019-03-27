from .compat import unittest
import ucl

class LoadTest(unittest.TestCase):
    def test_no_args(self):
        with self.assertRaises(TypeError):
            ucl.load()

    def test_multi_args(self):
        with self.assertRaises(TypeError):
            ucl.load(0,0)

    def test_none(self):
        self.assertEqual(ucl.load(None), None)

    def test_null(self):
        data  = "a: null"
        valid = { "a" : None }
        self.assertEqual(ucl.load(data), valid)

    def test_int(self):
        data  = "a : 1"
        valid = { "a" : 1 }
        self.assertEqual(ucl.load(data), valid)

    def test_braced_int(self):
        data  = "{a : 1}"
        valid = { "a" : 1 }
        self.assertEqual(ucl.load(data), valid)

    def test_nested_int(self):
        data  = "a : { b : 1 }"
        valid = { "a" : { "b" : 1 } }
        self.assertEqual(ucl.load(data), valid)

    def test_str(self):
        data  = "a : b"
        valid = { "a" : "b" }
        self.assertEqual(ucl.load(data), valid)

    def test_float(self):
        data  = "a : 1.1"
        valid = {"a" : 1.1}
        self.assertEqual(ucl.load(data), valid)

    def test_boolean(self):
        data = (
            "a : True;" \
            "b : False"
        )
        valid = { "a" : True, "b" : False }
        self.assertEqual(ucl.load(data), valid)

    def test_empty_ucl(self):
        self.assertEqual(ucl.load("{}"), {})

    def test_single_brace(self):
        self.assertEqual(ucl.load("{"), {})

    def test_single_back_brace(self):
        self.assertEqual(ucl.load("}"), {})

    def test_single_square_forward(self):
        self.assertEqual(ucl.load("["), [])

    def test_invalid_ucl(self):
        with self.assertRaisesRegex(ValueError, "unfinished key$"):
            ucl.load('{ "var"')

    def test_comment_ignored(self):
        self.assertEqual(ucl.load("{/*1*/}"), {})

    def test_1_in(self):
        valid = { 'key1': 'value' }
        with open("../tests/basic/1.in", "r") as in1:
            self.assertEqual(ucl.load(in1.read()), valid)

    def test_every_type(self):
        data = ("""{
            "key1": value;
            "key2": value2;
            "key3": "value;"
            "key4": 1.0,
            "key5": -0xdeadbeef
            "key6": 0xdeadbeef.1
            "key7": 0xreadbeef
            "key8": -1e-10,
            "key9": 1
            "key10": true
            "key11": no
            "key12": yes
        }""")
        valid = {
            'key1': 'value',
            'key2': 'value2',
            'key3': 'value;',
            'key4': 1.0,
            'key5': -3735928559,
            'key6': '0xdeadbeef.1',
            'key7': '0xreadbeef',
            'key8': -1e-10,
            'key9': 1,
            'key10': True,
            'key11': False,
            'key12': True,
            }
        self.assertEqual(ucl.load(data), valid)
