#include <iostream>
#include <string>
#include "ucl++.h"

int main(int argc, char **argv)
{
	std::string input, err;

	input.assign((std::istreambuf_iterator<char>(std::cin)),
		std::istreambuf_iterator<char>());

	auto obj = ucl::Ucl::parse(input, err);

	if (obj) {
		std::cout << obj.dump(UCL_EMIT_CONFIG) << std::endl;

		for (const auto &o : obj) {
			std::cout << o.dump(UCL_EMIT_CONFIG) << std::endl;
		}
	}
	else {
		std::cerr << "Error: " << err << std::endl;

		return 1;
	}
}
