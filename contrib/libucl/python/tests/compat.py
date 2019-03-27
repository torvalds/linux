try: 
     import unittest2 as unittest 
except ImportError: 
     import unittest 

# Python 2.7 - 3.1
if not hasattr(unittest.TestCase, 'assertRaisesRegex'):
    unittest.TestCase.assertRaisesRegex = unittest.TestCase.assertRaisesRegexp
