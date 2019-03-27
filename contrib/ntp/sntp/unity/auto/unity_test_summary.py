#! python3
# ==========================================
#   Unity Project - A Test Framework for C
#   Copyright (c) 2015 Alexander Mueller / XelaRellum@web.de
#   [Released under MIT License. Please refer to license.txt for details]
#   Based on the ruby script by  Mike Karlesky, Mark VanderVoord, Greg Williams
# ==========================================
import sys
import os
import re
from glob import glob

class UnityTestSummary:
    def __init__(self):
        self.report = ''
        self.total_tests = 0
        self.failures = 0
        self.ignored = 0

    def run(self):
        # Clean up result file names
        results = []
        for target in self.targets:
            results.append(target.replace('\\', '/'))

        # Dig through each result file, looking for details on pass/fail:
        failure_output = []
        ignore_output = []

        for result_file in results:
            lines = list(map(lambda line: line.rstrip(), open(result_file, "r").read().split('\n')))
            if len(lines) == 0:
                raise Exception("Empty test result file: %s" % result_file)

            details = self.get_details(result_file, lines)
            failures = details['failures']
            ignores = details['ignores']
            if len(failures) > 0: failure_output.append('\n'.join(failures))
            if len(ignores) > 0: ignore_output.append('n'.join(ignores))
            tests,failures,ignored = self.parse_test_summary('\n'.join(lines))
            self.total_tests += tests
            self.failures += failures
            self.ignored += ignored

        if self.ignored > 0:
            self.report += "\n"
            self.report += "--------------------------\n"
            self.report += "UNITY IGNORED TEST SUMMARY\n"
            self.report += "--------------------------\n"
            self.report += "\n".join(ignore_output)

        if self.failures > 0:
            self.report += "\n"
            self.report += "--------------------------\n"
            self.report += "UNITY FAILED TEST SUMMARY\n"
            self.report += "--------------------------\n"
            self.report += '\n'.join(failure_output)

        self.report += "\n"
        self.report += "--------------------------\n"
        self.report += "OVERALL UNITY TEST SUMMARY\n"
        self.report += "--------------------------\n"
        self.report += "{total_tests} TOTAL TESTS {failures} TOTAL FAILURES {ignored} IGNORED\n".format(total_tests = self.total_tests, failures=self.failures, ignored=self.ignored)
        self.report += "\n"

        return self.report

    def set_targets(self, target_array):
            self.targets = target_array

    def set_root_path(self, path):
        self.root = path

    def usage(self, err_msg=None):
        print("\nERROR: ")
        if err_msg:
            print(err_msg)
        print("\nUsage: unity_test_summary.rb result_file_directory/ root_path/")
        print("     result_file_directory - The location of your results files.")
        print("                             Defaults to current directory if not specified.")
        print("                             Should end in / if specified.")
        print("     root_path - Helpful for producing more verbose output if using relative paths.")
        sys.exit(1)

    def get_details(self, result_file, lines):
        results = { 'failures': [], 'ignores': [], 'successes': [] }
        for line in lines:
            parts = line.split(':')
            if len(parts) != 5:
                continue
            src_file,src_line,test_name,status,msg = parts
            if len(self.root) > 0:
                line_out = "%s%s" % (self.root, line)
            else:
                line_out = line
            if status == 'IGNORE':
                results['ignores'].append(line_out)
            elif status == 'FAIL':
                results['failures'].append(line_out)
            elif status == 'PASS':
                results['successes'].append(line_out)
        return results

    def parse_test_summary(self, summary):
        m = re.search(r"([0-9]+) Tests ([0-9]+) Failures ([0-9]+) Ignored", summary)
        if not m:
            raise Exception("Couldn't parse test results: %s" % summary)

        return int(m.group(1)), int(m.group(2)), int(m.group(3))


if __name__ == '__main__':
  uts = UnityTestSummary()
  try:
    #look in the specified or current directory for result files
    if len(sys.argv) > 1:
        targets_dir = sys.argv[1]
    else:
        targets_dir = './'
    targets = list(map(lambda x: x.replace('\\', '/'), glob(targets_dir + '*.test*')))
    if len(targets) == 0:
        raise Exception("No *.testpass or *.testfail files found in '%s'" % targets_dir)
    uts.set_targets(targets)

    #set the root path
    if len(sys.argv) > 2:
        root_path = sys.argv[2]
    else:
        root_path = os.path.split(__file__)[0]
    uts.set_root_path(root_path)

    #run the summarizer
    print(uts.run())
  except Exception as e:
    uts.usage(e)
