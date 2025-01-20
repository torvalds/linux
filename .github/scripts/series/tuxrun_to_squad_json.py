import argparse
import json

"""
Turn
{
    "testsuiteA": {
        "test1": {}
    },
    "testsuiteB": {
        "test1": {}
    }
}

into

{
    "testsuiteA/test1": {}
    "testsuiteB/test1": {}
}

which is what is expected by Squad.
"""

def parse_args():
    parser = argparse.ArgumentParser(description = 'Output Squad tests results for tuxrun LTP')
    parser.add_argument("--result-path", default = "",
            help = 'Path to the tuxrun JSON result file')
    parser.add_argument("--testsuite", default = "",
            help = 'Testsuite name')

    return parser.parse_args()

def generate_squad_json(result_path, testsuite):
    dict_results = {}

    with open(result_path, "r") as f:
        dict_initial = json.loads(f.read())

    # Search only the first dimension for keys starting with "ltp-"
    for k, v in dict_initial.items():
        if k.startswith(testsuite):
            for ltp_key, ltp_value in v.items():
                dict_results[k + "/" + ltp_key] = ltp_value

    print(dict_results)

    with open(result_path.replace(".json", ".squad.json"), "w") as f:
        json.dump(dict_results, f)

if __name__ == "__main__":
    args = parse_args()
    generate_squad_json(args.result_path, args.testsuite)

